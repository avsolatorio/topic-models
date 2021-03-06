/*
 * Sub-variable likelihood calculations
 * Copyright (C) 2013-2014 Jinjing Li and Wray Buntine
 * All rights reserved.
 *
 * This Source Code Form is subject to the terms of the Mozilla 
 * Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at
 *      http://mozilla.org/MPL/2.0/.
 *
 * Author: Jinjing Li <jinjingli@gmail.com>
 *         Wray Buntine (wray.buntine@monash.edu)
 *
 *  Specialised probability calcs for parts of the model,
 *  nothing is changed
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <time.h>

#include "yap.h"
#include "util.h"
#include "stable.h"
#include "lgamma.h"
#include "tca.h"
#include "stats.h"

/*************************************
 *
 *     more or less mirrored routines for the 
 *     wordXtopic versus docXtopic
 *
 ************************************/

/*
 *   probability of topic from doc level PDP  
 *
 *       t    - topic
 *       (i,mi)    -  word index and corresponding multi version
 *       pK   - input contribution to posterior from adding word with topic
 *       *dip - set to prob. indicator would be 1, if NULL leave
 */
double docfact(D_MiSi_t *dD, int t, int i, int mi, double pK, float *dip) {
  int N = dD->Mi[t], S = dD->Si[t];
  int n, s;
  assert(dip);
  *dip = 1;
  if ( !PCTL_BURSTY() ) 
    return pK;
  if ( M_multi(i) ) {
    int mii;
    // assert(mi<ddM.dim_multiind || did==ddN.D-1);
    mii = ddM.multiind[mi]-dD->mi_base;
    assert(mii>=0);
    assert(mii<ddM.MI_max);
    n = dD->Mik[mii][t];
    s = dD->Sik[mii][t];
  } else {
    n = s = 0;
  }  
  if ( s==0 ) {
    return pK * (ddP.b_burst+ddP.a_burst*S)/(ddP.b_burst+N); 
  } else {
    double one = pK * (ddP.b_burst+ddP.a_burst*S) * (s+1.0)/(n+1.0);
    double zero = (n-s+1.0)/(n+1.0);
    one *= S_UV(ddC.a_burst,n,s+1);
    if ( s==1 )
      zero *= n - ddP.a_burst;
    else
      zero *= S_U(ddC.a_burst,n,s);    
    *dip = one/(one + zero);
    return (one + zero) /(ddP.b_burst+N);
  }
  return 0;
}

/*
 *   counterpart to docfact()
 *   only used in estimation, (ddP.bdk!=NULL version)
 */
double docprob(D_MiSi_t *dD, int t, int i, int mi, double pw) {
  int N = dD->Mi[t], S = dD->Si[t];
  int n, s;
  if ( M_multi(i) ) {
    int mii;
    // assert(mi<ddM.dim_multiind || did==ddN.D-1);
    mii = ddM.multiind[mi]-dD->mi_base;
    assert(mii>=0);
    assert(mii<ddM.MI_max);
    n = dD->Mik[mii][t];
    s = dD->Sik[mii][t];
  } else {
    n = s = 0;
  }  
  if ( s==0 ) {
    return pw * (ddP.b_burst+ddP.a_burst*S)/(ddP.b_burst+N); 
  } 
  return (pw * (ddP.b_burst+ddP.a_burst*S) + (n-ddP.a_burst*s))
    /(ddP.b_burst + N);
}


//binomal (log)
double log_binom(int a, int b){
  assert(b>=a);
  int i,s;
  long result=0;
  if (b>a) {
        s=a;
  } else
        s=b;
  for (i=1; i<=s ;i++) {
    result += log(b-i+1);
    result -= log(i);
  }
  return result;
}

/*
 *    the various node probabilities for
 *           VAR_one_fact()  --  multiplicity is incremented
 *           VAR_zero_fact() --   multiplicity is unchanged
 *    have special handling for when n==0
 */
static double theta_one_fact(int d, int t) {
  int n = ddS.n_dt[d][t];
  double fact = 1;
  if ( n>0 ) {
    int c = ddS.c_dt[d][t];
    if ( c<=0 ) 
      c=1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_theta, n, c+1) * (c+1.0)/(n+1);
    else if (n==c+1)
      fact = n/(n-1.0);
  }
  return fact * 
    (ddP.b_theta + ddP.a_theta * ddS.C_dT[d]) / (ddP.b_theta + ddS.N_dT[d]);
}
static double theta_zero_fact(int d, int t) {
  int n = ddS.n_dt[d][t];
  int c;
  if ( n==0 )
    return 0;
  c = ddS.c_dt[d][t];
  if ( c<=0 ) 
    c=1;
  return 
    ((n==1)?((1-ddP.a_theta)/2.0):(S_U(ddC.a_theta, n, c) * (n-c+1.0)/(n+1)))
    / (ddP.b_theta + ddS.N_dT[d]);
}
/*
 * reorganises call to save on vector lookups
 *  BUT testing show its 10% slower with mufact
 */
//  #define mufact
#ifdef mufact
//    Z += mu_fact(e,t, &Y);
static double mu_fact(int e, int t, double *Y) {
  int n = ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0);
  double denom = (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
  double renum = (ddP.b_mu[e] + ddP.a_mu * ddS.Cp_e[e]);
  if ( n==0 ) {
    *Y *= renum/denom;
    return 0;
  } else {
    double fact = 1.0;
    int c = ddS.cp_et[e][t];
    double oldY = *Y;
    if ( c<=0 ) c = 1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_mu, n, c+1) * (c+1.0)/(n+1);    
    else if (n==c+1)
      fact = n/(n-1.0);
    *Y *= fact * renum/denom;
    return oldY * ((n==1)?((1-ddP.a_mu)/2.0):(S_U(ddC.a_mu, n, c) * (n-c+1.0)/(n+1)))
      / denom;
  }
}
#else
static double mu_one_fact(int e, int t) {
  int n = ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0);
  double fact = 1;
  if ( n>0 ) {
    int c = ddS.cp_et[e][t];
    if ( c<=0 )
      c = 1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_mu, n, c+1) * (c+1.0)/(n+1);    
    else if (n==c+1)
      fact = n/(n-1.0);
  }
  return fact * (ddP.b_mu[e] + ddP.a_mu * ddS.Cp_e[e]) ;
}
static double mu_zero_fact(int e, int t) {
  int n = ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0);
  int c;
  if ( n==0 )
    return 0;
  c = ddS.cp_et[e][t];
  if ( c<=0 )
    c = 1;
  return
    ((n==1)?((1-ddP.a_mu)/2.0):(S_U(ddC.a_mu, n, c) * (n-c+1.0)/(n+1))) ;
}
static double mu_norm_fact(int e) {
  return (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
}
#endif
static double mu0_prob(int t) {
  return (ddS.cp_et[0][t] + ddP.b_mu0/ddN.T)/(ddS.Cp_e[0]+ddP.b_mu0);
}


//    Z += Y * phi_zero_fact(e, v, t);
//    Y *= phi_one_fact(e, v, t);
#define phi_one_fact_plus(e,t) (ddP.b_phi[e][t] + ddP.a_phi1 * ddS.S_Vte[t][e])
static double phi_one_fact(int e, int v, int t) {
  int n = ddS.m_vte[v][t][e] + ((e<ddN.E-1)?ddS.s_vte[v][t][e+1]:0);
  double fact = 1;
  if ( n>0 ) {
    int c = ddS.s_vte[v][t][e];
    if ( c<=0 )
      c = 1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_phi1, n, c+1) * (c+1.0)/(n+1);
    else if (n==c+1)
      fact = n/(n-1.0);
  }
  return fact; //  * (ddP.b_phi[e][t] + ddP.a_phi1 * ddS.S_Vte[t][e]);
}
static double phi_zero_fact(int e, int v, int t) {
  int n = ddS.m_vte[v][t][e] + ((e<ddN.E-1)?ddS.s_vte[v][t][e+1]:0);
  int c;
  if ( n==0 )
    return 0;
  c = ddS.s_vte[v][t][e];
  if ( c<=0 )
    c = 1;
  return S_U(ddC.a_phi1, n, c) * (n-c+1.0)/(n+1);
}
static double phi_norm_fact(int e, int t) {
  return (ddP.b_phi[e][t] + ddS.M_Vte[t][e] + ((e<ddN.E-1)?ddS.S_Vte[t][e+1]:0));
}

#ifdef PHI_CACHE
/*   
 *     = phi_norm_fact(e,t)  
 *
 *  needs to be updated before every sampling
 *  whenever ddS.S_Vte is changed
 */
static double **phi_norm_cache = NULL;
/*   cache is valid up to this e */
static int phi_norm_cache_e;
/*   cache needs changing back to this e */
static int *phi_norm_cache_backe;
static int **phi_unit_cache_backe;

/*
 *     final cached values
 */
static double ***phi_sum_cache = NULL;
static double ***phi_one_cache = NULL;
static double ***phi_zero_cache = NULL;

/*   last word index for -- (w)  */
static int *phi_last_posn;
/*   largest word index updated to epoch e -- (k,e) */
static int **phi_posn_inv;

static double ***mt3(int a, int b, int c) {
  double ***t3 = malloc(a*sizeof(t3[0]));
  int i;
  for (i=0; i<a; i++)
    t3[i] = dmat(b,c);
  if ( t3[i-1]==NULL )
    yap_quit("Cannot allocate memory in phi_norm_init()\n");
  return t3;
}
static void ft3(double ***t3, int a) {
  int i;
  for (i=0; i<a; i++) {
    free(t3[i][0]);
    free(t3[i]);
  }
  free(t3);
}

void phi_cache_init() {
  int i;
  phi_norm_cache = dmat(ddN.T,ddN.E);
  phi_norm_cache_backe = malloc(ddN.T*sizeof(phi_norm_cache_backe[0]));
  //  make this 
  phi_unit_cache_backe = malloc(ddN.W*sizeof(phi_unit_cache_backe[0]));
  phi_unit_cache_backe[0] = malloc(ddN.T*ddN.W*sizeof(phi_unit_cache_backe[0][0]));
  for (i=1; i<ddN.W; i++)
    phi_unit_cache_backe[i] = phi_unit_cache_backe[i-1]+ddN.T;
  //  make this 
  phi_last_posn = malloc(ddN.W*sizeof(phi_last_posn[0]));
  //  make this 
  phi_posn_inv = malloc(ddN.T*sizeof(phi_posn_inv[0]));
  phi_posn_inv[0] = malloc(ddN.T*ddN.E*sizeof(phi_posn_inv[0][0]));
  for (i=1; i<ddN.T; i++)
    phi_posn_inv[i] = phi_posn_inv[i-1]+ddN.E;
  //  make these too
  phi_sum_cache = mt3(ddN.W,ddN.T,ddN.E);
  phi_one_cache = mt3(ddN.W,ddN.T,ddN.E);
  phi_zero_cache = mt3(ddN.W,ddN.T,ddN.E);
  phi_cache_reinit();
}

void phi_cache_reinit() {
  int w, t, e;
  for (t=0; t<ddN.T; t++) {
    phi_norm_cache_backe[t] = 0;
    for (e=0; e<ddN.E; e++)
      phi_posn_inv[t][e] = ddN.N;
  }
  for (w=0; w<ddN.W; w++) {
    phi_last_posn[w] = 0;
    for (t=0; t<ddN.T; t++) 
      phi_unit_cache_backe[w][t] = 0;
  }
  phi_norm_cache_e = -1;
}

void phi_cache_free() {
  assert(phi_norm_cache);
  free(phi_norm_cache[0]);
  free(phi_norm_cache);
  free(phi_norm_cache_backe);
  free(phi_unit_cache_backe[0]);
  free(phi_unit_cache_backe);
  free(phi_posn_inv[0]);
  free(phi_posn_inv);
  free(phi_last_posn);
  ft3(phi_sum_cache,ddN.W);
  ft3(phi_one_cache,ddN.W);
  ft3(phi_zero_cache,ddN.W);
}


/*
 *    compute epoch need to update from,
 *    the previous one should be valid
 */
int phi_cache_backe(int k, int l) {
  int e;
  int laste = 0;
  return 0;
  assert(l>=0);
  assert(phi_norm_cache_e>=0);
  for (e = phi_norm_cache_e; e>=0 && phi_posn_inv[k][e]>=l; e--) {
    if ( phi_posn_inv[k][e]<ddN.N )
      laste = e;
  }
  return laste;
}

void phi_norm_change(int w, int t, int backe) {
  if ( backe<0 )
    backe = 0;
  if ( backe< phi_norm_cache_backe[t] )
    phi_norm_cache_backe[t] = backe;
  if ( backe< phi_unit_cache_backe[w][t] )
    phi_unit_cache_backe[w][t] = backe;
}


/*
 *  may have just changed ddS.S_Vte 
 *  currently filled to e=phi_norm_cache_e
 *  want to fill to e=ce 
 */
void phi_norm_update(int w, int ce) {
  int e, t;
  if ( phi_norm_cache_e<ce ) {
    /*
     *   moving to new epoch so set backe[]'s
     */
    for (t=0; t<ddN.T; t++) {
      if ( phi_norm_cache_backe[t]>phi_norm_cache_e )
        phi_norm_cache_backe[t] = phi_norm_cache_e+1;
      if ( phi_unit_cache_backe[w][t]>phi_norm_cache_e )
        phi_unit_cache_backe[w][t] = phi_norm_cache_e+1;
    }
  }
  for (t=0; t<ddN.T; t++) {
    for (e=phi_norm_cache_backe[t]; e<=ce; e++) {
      phi_norm_cache[t][e] = phi_norm_fact(e, t);
    }
    phi_norm_cache_backe[t] = ce+1;
      //  cache works if make this start at zero ...????
    for (e=phi_unit_cache_backe[w][t]; e<=ce; e++) {
      phi_one_cache[w][t][e] = phi_one_fact(e, w, t);
      phi_zero_cache[w][t][e] = phi_zero_fact(e, w, t);
    }
    phi_unit_cache_backe[w][t] = ce+1;
  }
  phi_norm_cache_e = ce;
} 

/*
 *   if (phi_posn_inv[t][e]>ddN.N) then
 *      all cache of this topic invalid for epoch e
 *   else
 *      cache updated before posn l=phi_posn_inv[t][e]
 *      are invalid
 */
void phi_sum_change(int t, int e, int i) {
  if ( e<0 )
    e = 0;
  phi_posn_inv[t][e] = i;
  for (e++; e<=phi_norm_cache_e; e++)
    phi_posn_inv[t][e] = ddN.N+1;
}

/*
 *  may have just changed ddS.S_Vte 
 *  want to fill to e=ce 
 */
void phi_sum_update(int w, int ce, int i) {
  int e, t;
  // if ( ce>0 ) yap_message("w=%d,ce=%d: ", w, ce);
  for (t=0; t<ddN.T; t++) {
    double *norm = phi_norm_cache[t];
    double *zero = phi_zero_cache[w][t];
    double *one = phi_one_cache[w][t];
    double *sum = phi_sum_cache[w][t];
    e = phi_cache_backe(t, phi_last_posn[w]);
    // if ( ce>0 ) yap_message("%d/%d ", phi_last_posn[w], e);
    if ( e>ce )
      continue;
    if ( e<=0 ) {
      sum[0] = (zero[0] + one[0] * phi_one_fact_plus(0,t)* phi0_prob(w)) / norm[0];
      e = 1;
    }
    for ( ; e<=ce; e++) {
      sum[e] = (zero[e] + one[e]*phi_one_fact_plus(e,t)*sum[e-1]) / norm[e];
    }
  }
  phi_last_posn[w] = i;
  // if ( ce>0 ) yap_message("\n");
} 
#endif
double phi0_prob(int v) {
  double term;
  if ( ddS.S_0vT[v]==0 )
    /*   spread the zero weight over all zero */
    term = (ddP.b_phi0 + ddP.a_phi0*ddS.S_0_nz)
      /(ddN.W-ddS.S_0_nz);
  else
    term = ddS.S_0vT[v] - ddP.a_phi0;
  return term/(ddP.b_phi0 + ddS.S_0);
}

/*
 *   normally the normaliser term is forgotten for topicfact(),
 *   but we need to add it in when doing the doc PYP
 */
double topicnorm(int d) {
  return ((double)ddS.N_dT[d]+ddP.b_theta);
}

/*
 *    mu_prob() but do one epoch at a time
 */
void mu_prob_iter(int e, double *vec) {
  int t;
  if ( e<0 ) {
    for (t=0; t<ddN.T; t++)
      vec[t] = mu0_prob(t);
  } else {
    double norm = (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
    double pwght = (ddP.b_mu[e] + ddP.a_mu * ddS.Cp_e[e]);
    for (t=0; t<ddN.T; t++) {
      vec[t] =  ( (ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0)
                   - ddP.a_mu * ddS.cp_et[e][t]) + pwght*vec[t] ) /norm;
    }
  }
}

static double mu_prob(int e, int t) { 
  double prob;
  if ( ddP.mu )
    return ddP.mu[e][t];
  if ( e==0 )
    prob = mu0_prob(t);
  else
    prob = mu_prob(e-1,t);
  return ( (ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0)
	    - ddP.a_mu * ddS.cp_et[e][t])
	   + (ddP.b_mu[e] + ddP.a_mu * ddS.Cp_e[e])*prob )
    / (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
}

double word_side_prob(int e, int v, int t) { 
  double prob;
  if ( ddP.phi )
    return ddP.phi[e][v][t];
  if ( e==0 )
    prob = phi0_prob(v);
  else
    prob = word_side_prob(e-1,v,t);
  return ( (ddS.m_vte[v][t][e] + ((e<ddN.E-1)?ddS.s_vte[v][t][e+1]:0)
	    - ddP.a_phi1 * ddS.s_vte[v][t][e])
	   + (ddP.b_phi[e][t] + ddP.a_phi1 * ddS.S_Vte[t][e])*prob )
    / (ddP.b_phi[e][t] + ddS.M_Vte[t][e] + ((e<ddN.E-1)?ddS.S_Vte[t][e+1]:0));					    
}

/*
 *     word_side_prob() but done iteratively by epoch
 */
void phi_prob_iter(int e, double **mtx) { 
  int v, t;
  if ( e<0 ) {
    for (v=0; v<ddN.W; v++)
      for (t=0; t<ddN.T; t++)
        mtx[v][t] = phi0_prob(v);
    return;
  } else {
    double norm[ddN.T];
    double wght[ddN.T];
    for (t=0; t<ddN.T; t++) {
      norm[t] = (ddP.b_phi[e][t] + ddS.M_Vte[t][e] 
                 + ((e<ddN.E-1)?ddS.S_Vte[t][e+1]:0));
      wght[t] = (ddP.b_phi[e][t] + ddP.a_phi1 * ddS.S_Vte[t][e]);
    }
    for (v=0; v<ddN.W; v++)
      for (t=0; t<ddN.T; t++) 
        mtx[v][t] = ( (ddS.m_vte[v][t][e] + ((e<ddN.E-1)?ddS.s_vte[v][t][e+1]:0)
                       - ddP.a_phi1 * ddS.s_vte[v][t][e]) + wght[t]*mtx[v][t] )
          / norm[t];
  }
} 

double doc_side_prob(int d, int t) { 
  int e = ddD.e[d];
  double prob = mu_prob(e, t);
  return ((ddS.n_dt[d][t]-ddP.a_theta*ddS.c_dt[d][t]) 
	  + (ddP.b_theta + ddP.a_theta * ddS.C_dT[d])*prob);
}

/*
 *   return count to place table back:
 *     -1 = no contribution to current alpha stats
 *     0 = no table back, just to current alpha
 *     1 = back to previous time
 *     e = back to initial epoch
 *   e+1 = back to root
 *
 *   cuts short if ddP.back in order, so
 *   return is >= ddP.back
 */
int doc_side_ind(int d, int t) {  
  double Z = 0;
  int e = ddD.e[d];
  double Y = 1;
  double Ze[ddN.E+1];
  int i;

  if ( ddP.mu ) {
    double Z0 = theta_zero_fact(d,t);
    Z = Z0 + theta_one_fact(d,t) * ddP.mu[e][t];
    if ( Z0 > rng_unit(rngp)*Z ) 
      return -1;
    return 0;
  }

  for (i=e ; i>=0; i--) {
#ifdef mufact
    Z += mu_fact(i, t, &Y);
#else
    Y /= mu_norm_fact(i);
    Z += Y * mu_zero_fact(i, t);
    Y *= mu_one_fact(i, t);
#endif
    Ze[i+1] = Z;
    /*   cannot break if zeros so back is forced!  */
    if ( i<=e-ddP.back && ddS.cp_et[i][t]>0 ) break;
  }
  if ( i<0 ) {
    Z += Y * mu0_prob(t);
    Ze[0] = Z;
  }
  Z += theta_zero_fact(d,t)/theta_one_fact(d,t);
  Z *= rng_unit(rngp);
  if ( Z>Ze[i+1] )
    return -1;
  i++;
  for ( ; i<=e; i++) {
    if ( Z>Ze[i+1] )
      return e-i+1;
  }
  return 0;
}

#if 0 
/*
 *  iterative version of mu_side_fact_rec() is slower
 */
// calculate p(z=t) from doc side using r1 (\sum_r1{P(z=t,r1)})
static double mu_side_fact (int d, int t) {
  double Z = 0;
  int e = ddD.e[d];
  double Y = 1;
  int back = e-ddP.back;

  for ( ; e>=0; e--) {
#ifdef mufact
    Z += mu_fact(e, t, &Y);
#else
    Y /= mu_norm_fact(e);
    Z += Y * mu_zero_fact(e, t);
    Y *= mu_one_fact(e, t);
#endif
      if ( e>0 && e<=back && ddS.cp_et[e][t]>0 ) break;
  }
  if ( e<0 )
    Z += Y * mu0_prob(t);
  return Z;
}
#endif

#ifdef MU_CACHE
/*   = mu_side_fact(e,t)  */
static double **mu_side_fact_cache = NULL;
/*   cache is valid up to this e */
static int mu_side_fact_cache_e = -1;
/*   cache needs changing back to this e */
static int mu_side_fact_cache_backe = -1;

void mu_side_fact_init() {
  mu_side_fact_cache = dmat(ddN.E,ddN.T);
  mu_side_fact_cache_e = -1;
  mu_side_fact_cache_backe = -1;
}
void mu_side_fact_reinit() {
  mu_side_fact_cache_e = -1;
  mu_side_fact_cache_backe = -1;
}
void mu_side_fact_free() {
  assert(mu_side_fact_cache);
  free(mu_side_fact_cache[0]);
  free(mu_side_fact_cache);
}
void mu_side_fact_change(int backe) {
  if ( backe< mu_side_fact_cache_backe )
    mu_side_fact_cache_backe = backe;
}
/*
 *  must update mu_side_fact_cache[e][t]
 *  currently filled to e=mu_side_fact_cache_e
 *  want to fill to e=ce 
 */
void mu_side_fact_update(int ce) {
  int e, t;
  if ( mu_side_fact_cache_e < ce ) {
    if ( mu_side_fact_cache_backe> mu_side_fact_cache_e+1 )
      mu_side_fact_cache_backe = mu_side_fact_cache_e+1;
  } else {
    if ( mu_side_fact_cache_backe>ce )
      return;
  }
  if ( mu_side_fact_cache_backe<=0 ) {
    double Z = mu_norm_fact(0);
    for (t=0; t<ddN.T; t++) 
      mu_side_fact_cache[0][t] = 
        (mu_zero_fact(0, t) + mu_one_fact(0, t) * mu0_prob(t))/Z;
    mu_side_fact_cache_backe = 1;
  }
  for (e=mu_side_fact_cache_backe; e<=ce; e++) {
    double Z = mu_norm_fact(e);
    for (t=0; t<ddN.T; t++) 
      mu_side_fact_cache[e][t] = 
        (mu_zero_fact(e, t) + mu_one_fact(e, t) * mu_side_fact_cache[e-1][t])/Z;
  }
  mu_side_fact_cache_e = ce;
  mu_side_fact_cache_backe = ce+1;
} 
#endif
static double mu_side_fact_rec (int e, int t) {
  if ( e<=0 ) 
    return (mu_zero_fact(0, t) + mu_one_fact(0, t) * mu0_prob(t))
      / mu_norm_fact(0);
  return (mu_zero_fact(e, t) + mu_one_fact(e, t) * mu_side_fact_rec(e-1,t)) 
    / mu_norm_fact(e);
}
double doc_side_fact (int d, int t) {
    int e = ddD.e[d];
    return theta_zero_fact(d,t) + theta_one_fact(d,t) 
#ifdef MU_CACHE
      * ((ddP.mu!=NULL)?ddP.mu[e][t]:mu_side_fact_cache[e][t]);
#else
      * ((ddP.mu!=NULL)?ddP.mu[e][t]:mu_side_fact_rec(e,t));
#endif
}
/*
 *   return count to place table back:
 *     0 = no table back
 *     1 = back to previous time
 *     e = back to initial epoch
 *   e+1 = back to root
 *
 *   cuts short if ddP.back in order, so
 *   return is >= ddP.back
 */
int word_side_ind ( int e, int v, int t) {
  double Z = 0;
  double Y = 1;
  double Ze[ddN.E];
  int i;
#ifdef PHI_CACHE
  double *norm = phi_norm_cache[t];
  double *one = phi_one_cache[v][t];
  double *zero = phi_zero_cache[v][t];
#endif

  if ( ddP.phi )
    return 0;

  for (i=e ; i>=0; i--) {
#ifdef PHI_CACHE
    Y /= norm[i];
    Z += Y * zero[i];
    Y *= one[i]*phi_one_fact_plus(i,t);
#else
    Y /= phi_norm_fact(i, t);
    Z += Y * phi_zero_fact(i, v, t);
    Y *= phi_one_fact(i, v, t);
#endif
    Ze[i] = Z;
    /*   cannot break if zeros so back is forced!  */
    if ( i<=e-ddP.back && ddS.s_vte[v][t][i]>0 ) break;
  }
  if ( i<0 ) {
    Z += Y * phi0_prob(v);
  }
  i++;
  Z *= rng_unit(rngp);
  for ( ; i<=e; i++) {
    if ( Z>Ze[i] )
      return e-i+1;
  }
  return 0;
}

#ifndef NDEBUG
double word_side_fact_nocache ( int e, int v, int t) {
  double Z = 0;
  double Y = 1;
  int back = e-ddP.back;

  for ( ; e>=0; e--) {
    Y /= phi_norm_fact(e, t);
    Z += Y * phi_zero_fact(e, v, t);
    Y *= phi_one_fact(e, v, t);
    if ( e>0 && e<=back && ddS.s_vte[v][t][e]>0 ) return Z;
  }
  Z += Y * phi0_prob(v);
  return Z;
}
#endif

#define TESTPHI
double word_side_fact ( int e, int v, int t) {
#if defined(TESTPHI) && !defined(NDEBUG)
  int ein = e;
#endif
  if ( ddP.phi )
    return ddP.phi[e][v][t];
#ifdef PHI_CACHE
#if defined(TESTPHI) && !defined(NDEBUG)
  {
    double phi = word_side_fact_nocache(ein,v,t);
    double Z = phi_sum_cache[v][t][e];
    if ( fabs((phi-Z)/phi)>0.001 ) {
      yap_message("word_side_fact(%d,%d,%d)=%lf, actual=%lf\n",
		  ein, v, t, Z, phi);
    }
  }
#endif 
  return phi_sum_cache[v][t][e];
#else
  double Z = 0;
  double Y = 1;
  int back = e-ddP.back;
#ifdef PHI_CACHE
  double *norm = phi_norm_cache[t];
  double *one = phi_one_cache[v][t];
  double *zero = phi_zero_cache[v][t];
#endif

  for ( ; e>=0; e--) {
#ifdef PHI_CACHE
    Y /= norm[e];
    Z += Y * zero[e];
    Y *= one[e]*phi_one_fact_plus(i,t);
#else
    Y /= phi_norm_fact(e, t);
    Z += Y * phi_zero_fact(e, v, t);
    Y *= phi_one_fact(e, v, t);
#endif
    if ( e>0 && e<=back && ddS.s_vte[v][t][e]>0 ) return Z;
  }
  Z += Y * phi0_prob(v);
#if defined(TESTPHI) && !defined(NDEBUG)
  {
    double phi = word_side_fact_nocache(ein,v,t);
    if ( fabs((phi-Z)/phi)>0.00 ) {
      yap_message("word_side_fact(%d,%d,%d)=%lf, actual=%lf\n",
		  ein, v, t, Z, phi);
    }
  }
#endif
  return Z;
#endif
}



