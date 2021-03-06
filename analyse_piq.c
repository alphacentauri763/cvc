/****************************************************
 * analyse_piq.c
 *
 * Thu Oct  8 20:04:05 CEST 2009
 *
 * PURPOSE:
 * - c-implementation of analysis programme for \Pi_{\mu\nu}(\hat{q}^2)
 * DONE:
 * - tested qhat2-orbits and averaging against older contractions/cvc/analyse_piq
 * TODO:
 * - check cylinder and cone cut
 ****************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef MPI
#  include <mpi.h>
#endif
#include <getopt.h>

#define MAIN_PROGRAM

#include "cvc_complex.h"
#include "cvc_linalg.h"
#include "global.h"
#include "cvc_geometry.h"
#include "cvc_utils.h"
#include "mpi_init.h"
#include "io.h"
#include "io_utils.h"
#include "propagator_io.h"
#include "Q_phi.h"
#include "make_H3orbits.h"
#include "make_q2orbits.h"
#include "make_cutlist.h"
#include "get_index.h"
#include "read_input_parser.h"
#include "contractions_io.h"

#define _POW2(_a) ((_a)*(_a))
#define _POW4(_a) ((_a)*(_a)*(_a)*(_a))
#define _POW6(_a) ((_a)*(_a)*(_a)*(_a)*(_a)*(_a))
#define _POW8(_a) ((_a)*(_a)*(_a)*(_a)*(_a)*(_a)*(_a)*(_a))

void usage(void) {
  fprintf(stdout, "Program analyse_piq\n");
  fprintf(stdout, "Options:\n");
  fprintf(stdout, "-m INTEGER - mode of usage [default mode = 0]\n");
  fprintf(stdout, "             0 -> all; 1 -> averaging without cuts; 2 -> cylinder/cone cuts\n");
  /* ... */
  exit(0);
}

/***********************************************************************/

int main(int argc, char **argv) {

  int c, i, read_flag=0, iconf, status;
  int verbose=0;
  int mode=0, ntag=0;
  int filename_set=0;
  int l_LX_at, l_LXstart_at;
  int x0, x1, x2, x3, mu, nu, ix;
  int *q2id=(int*)NULL, *qhat2id=(int*)NULL, *q4id=(int*)NULL, *q6id=(int*)NULL, *q8id=(int*)NULL;
  int q2count, qhat2count, *picount=(int*)NULL;
  int *workid = (int*)NULL;
  int *h4_count=(int*)NULL, *h4_id=(int*)NULL, h4_nc;
  int *h3_count=(int*)NULL, *h3_id=(int*)NULL, h3_nc;
  int proj_type = 0;
  int check_WI = 0;
  int check_wi_xspace = 0;
  int force_byte_swap = 0;
  int qhat2id_filename_set=0;
  double *pimn=(double*)NULL;
  double *pi=(double*)NULL, piq, deltamn, *piavg=(double*)NULL;
  double q[4], qhat[4], q2, qhat2, *q2list=(double*)NULL, *qhat2list=(double*)NULL;
  double **h4_val=(double**)NULL, **h3_val=(double**)NULL;
  char filename[800], qhat2id_filename[400];
  complex w;
  double qval[4];
  FILE *ofs;
  int read_pimn0 = 0;
  char pimn0_filename[200];
  double pimn0_value[32];

  while ((c = getopt(argc, argv, "bh?vawWf:m:n:t:s:r:z:")) != -1) {
    switch (c) {
    case 'v':
      verbose = 1;
      break;
    case 'a':
      read_flag = 1;
      break;
    case 'f':
      strcpy(filename, optarg);
      filename_set = 1;
      break;
    case 'm':
      mode = atoi(optarg);
      break;
    case 't':
      proj_type = atoi(optarg);
      break;
    case 'n':
      ntag = atoi(optarg);
      break;
    case 'w':
      check_wi_xspace = 1;
      fprintf(stdout, "\n# [analyse_piq] will check Ward identity in position space and then exit\n");
      break;
    case 'W':
      check_WI = 1;
      fprintf(stdout, "\n# [analyse_piq] will check Ward identity in momentum space\n");
      break;
    case 'b':
      force_byte_swap = 1;
      fprintf(stdout, "\n# [analyse_piq] will enforce byte swap\n");
      break;
    case 's':
      qhat2id_filename_set = 1;
      strcpy(qhat2id_filename, optarg);
      fprintf(stdout, "\n# [analyse_piq] will save qhat2 id in file %s\n", qhat2id_filename);
      break;
    case 'r':
      qhat2id_filename_set = 2;
      sscanf(optarg, "%s %d", qhat2id_filename, &qhat2count );
      fprintf(stdout, "\n# [analyse_piq] will read qhat2 id from file %s, number of orbits is %d\n",
          qhat2id_filename, qhat2count);
      break;
    case 'z':
      read_pimn0 = 1;
      strcpy(pimn0_filename, optarg);
      fprintf(stdout, "# [] will read Pi_mn(0) from file %s\n", pimn0_filename);
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  // global time stamp
  g_the_time = time(NULL);
  if(g_proc_id == 0) fprintf(stdout, "\n# [analyse_piq] using global time_stamp %s", ctime(&g_the_time));
  

  /**************************
   * set the default values *
   **************************/
  if(filename_set==0) strcpy(filename, "analyse.input");
  fprintf(stdout, "# [analyse_piq] Reading input from file %s\n", filename);
  read_input_parser(filename);

  /*********************************
   * some checks on the input data *
   *********************************/
  if((T_global == 0) || (LX==0) || (LY==0) || (LZ==0)) {
    if(g_proc_id==0) fprintf(stderr, "\n[analyse_piq] Error, T and L's must be set\n");
    usage();
  }

  T            = T_global;
  Tstart       = 0;
  l_LX_at      = LX;
  l_LXstart_at = 0;

  if(init_geometry() != 0) {
    fprintf(stderr, "\n[analyse_piq] ERROR from init_geometry\n");
    return(102);
  }

  geometry();

  /* allocating memory for pimn */
  pimn = (double*)malloc(32*(size_t)VOLUME*sizeof(double));
  if(pimn==(double*)NULL) {
    fprintf(stderr, "\n[analyse_piq] Error, could not allocate memory for pimn\n");
    return(101);
  }

  pi = (double*)malloc(2*(size_t)VOLUME*sizeof(double));
  if(pi==(double*)NULL) {
    fprintf(stderr, "\n[analyse_piq] Error, could not allocate memory for pi\n");
    return(103);
  }

  /***************************************
   * allocate mem for id lists
   ***************************************/
  q2id = NULL;
  //q2id = (int*)malloc(VOLUME*sizeof(int));
  //if(q2id==(int*)NULL) {
  //  fprintf(stderr, "\n[analyse_piq] could not allocate memory for q2id\n");
  //  return(105);
  //}
/*
  q4id = (int*)malloc(VOLUME*sizeof(int));
  if(q4id==(int*)NULL) {
    fprintf(stderr, "could not allocate memory for q4id\n");
    return(115);
  }

  q6id = (int*)malloc(VOLUME*sizeof(int));
  if(q6id==(int*)NULL) {
    fprintf(stderr, "could not allocate memory for q6id\n");
    return(116);
  }

  q8id = (int*)malloc(VOLUME*sizeof(int));
  if(q8id==(int*)NULL) {
    fprintf(stderr, "could not allocate memory for q8id\n");
    return(117);
  }
*/

  qhat2id = (int*)malloc(VOLUME*sizeof(int));
  if(qhat2id==(int*)NULL) {
    fprintf(stderr, "\n[analyse_piq] Error, could not allocate memory for qhat2id\n");
    return(106);
  }

  workid = (int*)malloc(VOLUME*sizeof(int));
  if(workid==(int*)NULL) {
    fprintf(stderr, "\n[analyse_piq] Error, could not allocate memory for workid\n");
    return(106);
  }

  /***********************************
   * make lists for id, count and val
   ***********************************/
  if( qhat2id_filename_set == 1 ) {
    fprintf(stdout, "\n# [analyse_piq] making qid lists\n");
    if(make_qid_lists(q2id, qhat2id, &q2list, &qhat2list, &q2count, &qhat2count) != 0) {
      fprintf(stderr, "\n[analyse_piq] Error from make_qid_lists\n");
      return(122);
    }

    ofs = fopen(qhat2id_filename, "w");
    if(ofs==NULL) {
      fprintf(stderr, "\n[analyse_piq] Error, could not open file %s for writing\n", qhat2id_filename);
      exit(123);
    }
    if( fwrite(qhat2id, sizeof(int), VOLUME, ofs) != VOLUME || \
        fwrite(qhat2list, sizeof(double), qhat2count, ofs) != qhat2count ) {
      fprintf(stderr, "\n[analyse_piq] Error, could not write prop amount of items to file %s\n", qhat2id_filename);
      exit(127);
    }
    fclose(ofs);
  } else if( qhat2id_filename_set == 2 ) {
    // allocate memory for qhat2list
    if( (qhat2list = (double*)malloc(qhat2count*sizeof(double))) == NULL ) {
      fprintf(stderr, "\nError, could not alloc memory for qhat2list\n");
      exit(125);
    }
    if( (ofs = fopen(qhat2id_filename, "r")) == NULL ) {
      fprintf(stderr, "\n[analyse_piq] Error, could not open file %s for reading\n", qhat2id_filename);
      exit(124);
    }
    if( fread(qhat2id, sizeof(int), VOLUME, ofs) != VOLUME ) {
      fprintf(stderr, "\n[analyse_piq] Error, could not read prop amount of items from file %s\n", qhat2id_filename);
      exit(126);
    }
    if( fread(qhat2list, sizeof(double), qhat2count, ofs) != qhat2count ) {
      fprintf(stderr, "\n[analyse_piq] Error, could not read prop amount of items from file %s\n", qhat2id_filename);
      exit(126);
    }
    fclose(ofs);
  } else {
    fprintf(stdout, "\n# [analyse_piq] making qid lists\n");
    if(make_qid_lists(q2id, qhat2id, &q2list, &qhat2list, &q2count, &qhat2count) != 0) {
      fprintf(stderr, "\n[analyse_piq] Error from make_qid_lists\n");
      return(122);
    }
  }
  fprintf(stdout, "\n# [analyse_piq] number of qhat2 orbits = %d\n", qhat2count);

  if(mode==0 || mode==3) {
    fprintf(stdout, "\n# [analyse_piq] make H3 orbits\n");  
    if(make_H3orbits(&h3_id, &h3_count, &h3_val, &h3_nc) != 0) return(123);
  }

  if(mode==0 || mode==4) {
    fprintf(stdout, "\n# [analyse_piq] make H4 orbits\n");  
    if(make_H4orbits(&h4_id, &h4_count, &h4_val, &h4_nc) != 0) return(124);
  }
  fprintf(stdout, "\n# [analyse_piq] finished making qid lists\n");

/*******************************************
 * loop on the configurations
 *******************************************/
// for(iconf=g_gaugeid; iconf<=g_gaugeid2; iconf+=g_gauge_step) {

  for(ix=0;ix<32*VOLUME;ix++) pimn[ix] = 0.;

  if(read_pimn0) {
    ofs = fopen(pimn0_filename, "r");
    for(i=0; i<16;i++) {
      fscanf(ofs, "%lf%lf", pimn0_value+2*i, pimn0_value+2*i+1);
    }
    fclose(ofs);
    fprintf(stdout, "# [] Pi_mn(0)\n");
    for(i=0; i<16;i++) {
      fprintf(stdout, "\t%3d%3d%16.7e%16.7e\n", i/4, i%4, pimn0_value[2*i], pimn0_value[2*i+1]);
    }
  }

  Nconf = iconf;
  fprintf(stdout, "\n# [analyse_piq] iconf = %d\n", iconf);

  /****************
   * reading pimn *
   ****************/
/*
  if(read_pimn(pimn, read_flag) != 0) {
    fprintf(stderr, "Error on reading of pimn\n");
    continue;
  }
*/
  //if(format != 2) {
  //  //sprintf(filename, "%s.%.4d.%.4d", filename_prefix, iconf, Nsave);
  //  sprintf(filename, "%s.%.4d", filename_prefix, iconf);
  //} else {
    sprintf(filename, "%s", filename_prefix);
  //}
  fprintf(stdout, "# [analyse_piq] Reading data from file %s\n", filename);
  status = read_lime_contraction(pimn, filename, 16, 0);
  if(status != 0) {
    fprintf(stderr, "\n[analyse_piq] Error on reading of pimn, status was %d\n", status);
    // continue;
    exit(123);
  }

  if(force_byte_swap) {
    fprintf(stdout, "\n# [analyse_piq] starting byte swap ...");
    byte_swap64_v2(pimn, 32*VOLUME);
    fprintf(stdout, " done\n");
  }

  /* test: write the contraction data */
/*
  for(x0=0; x0<T; x0++) {
  for(x1=0; x1<LX; x1++) {
  for(x2=0; x2<LY; x2++) {
  for(x3=0; x3<LZ; x3++) {
    ix = g_ipt[x0][x1][x2][x3];
    fprintf(stdout, "# t=%3d, x=%3d, y=%3d, z=%3d\n", x0, x1, x2, x3);
    for(mu=0; mu<16; mu++) {
      fprintf(stdout, "%3d%25.16e%25.16e\n", mu, pimn[_GWI(mu,ix,VOLUME)], pimn[_GWI(mu,ix,VOLUME)+1]);
    }
  }}}}
*/
  /**********************************************
   * test the Ward identity in position space
   **********************************************/
  if(check_wi_xspace == 1) {
    ofs = fopen("WI_check_x", "w");
    for(x0=0; x0<T; x0++) {
    for(x1=0; x1<LX; x1++) {
    for(x2=0; x2<LY; x2++) {
    for(x3=0; x3<LZ; x3++) {
      ix = g_ipt[x0][x1][x2][x3];
      fprintf(ofs, "# t=%.2d, x=%.2d, y=%.2d, z=%.2d\n", x0, x1, x2, x3);
      for(mu=0; mu<4; mu++) {
        w.re = pimn[_GWI(0*4+mu,ix,VOLUME)  ] + pimn[_GWI(1*4+mu,ix,VOLUME)  ]
             + pimn[_GWI(2*4+mu,ix,VOLUME)  ] + pimn[_GWI(3*4+mu,ix,VOLUME)  ]
             - pimn[_GWI(0*4+mu,g_idn[ix][0],VOLUME)  ] - pimn[_GWI(1*4+mu,g_idn[ix][1],VOLUME)  ]
             - pimn[_GWI(2*4+mu,g_idn[ix][2],VOLUME)  ] - pimn[_GWI(3*4+mu,g_idn[ix][3],VOLUME)  ];
            
        w.im = pimn[_GWI(0*4+mu,ix,VOLUME)+1] + pimn[_GWI(1*4+mu,ix,VOLUME)+1]
             + pimn[_GWI(2*4+mu,ix,VOLUME)+1] + pimn[_GWI(3*4+mu,ix,VOLUME)+1]
             - pimn[_GWI(0*4+mu,g_idn[ix][0],VOLUME)+1] - pimn[_GWI(1*4+mu,g_idn[ix][1],VOLUME)+1]
             - pimn[_GWI(2*4+mu,g_idn[ix][2],VOLUME)+1] - pimn[_GWI(3*4+mu,g_idn[ix][3],VOLUME)+1];
        fprintf(ofs, "%3d%25.16e%25.16e\n", mu, w.re, w.im);
      }
    }}}}
    fclose(ofs);
    // continue; 
  }

  /**********************************************
   * test the Ward identity in momentum space
   **********************************************/
  if(check_WI==1) {
    ofs = fopen("WI_check", "w");
    for(x0=0; x0<T; x0++) {
      q[0] = 2. * sin( M_PI * (double)x0 / (double)T );
    for(x1=0; x1<LX; x1++) {
      q[1] = 2. * sin( M_PI * ((double)x1 + BCangle[1]*0.5)/ (double)LX );
    for(x2=0; x2<LY; x2++) {
      q[2] = 2. * sin( M_PI * ((double)x2 + BCangle[2]*0.5)/ (double)LY );
    for(x3=0; x3<LZ; x3++) {
      q[3] = 2. * sin( M_PI * ((double)x3 + BCangle[3]*0.5)/ (double)LZ );
      ix = g_ipt[x0][x1][x2][x3];
      fprintf(ofs, "# qt=%.2d, qx=%.2d, qy=%.2d, qz=%.2d\n", x0, x1, x2, x3);
      for(mu=0; mu<4; mu++) {
        w.re = q[0] * pimn[_GWI(0*4+mu,ix,VOLUME)  ] + q[1] * pimn[_GWI(1*4+mu,ix,VOLUME)  ]
             + q[2] * pimn[_GWI(2*4+mu,ix,VOLUME)  ] + q[3] * pimn[_GWI(3*4+mu,ix,VOLUME)  ];
        w.im = q[0] * pimn[_GWI(0*4+mu,ix,VOLUME)+1] + q[1] * pimn[_GWI(1*4+mu,ix,VOLUME)+1]
             + q[2] * pimn[_GWI(2*4+mu,ix,VOLUME)+1] + q[3] * pimn[_GWI(3*4+mu,ix,VOLUME)+1];
        fprintf(ofs, "%3d%25.16e%25.16e\n", mu, w.re, w.im);
      }
    }}}}
    fclose(ofs);
  }

  /**************************
   * calculate pi from pimn:
   **************************/
  fprintf(stdout, "\n# [analyse_piq] calculate pi from pimn\n");
  for(x0=0; x0<T; x0++) {
    q[0]    = 2. * sin( M_PI / (double)T_global * (double)(x0+Tstart) );
  for(x1=0; x1<LX; x1++) {
    q[1]    = 2. * sin( M_PI / (double)LX       * ((double)(x1)+BCangle[1]*0.5) );
  for(x2=0; x2<LY; x2++) {
    q[2]    = 2. * sin( M_PI / (double)LY       * ((double)(x2)+BCangle[2]*0.5) );
  for(x3=0; x3<LZ; x3++) {
    q[3]    = 2. * sin( M_PI / (double)LZ       * ((double)(x3)+BCangle[3]*0.5) );
    ix = g_ipt[x0][x1][x2][x3];
    q2    = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
    qval[0] = q2;
    qval[1] = _POW4(q[0]) + _POW4(q[1]) + _POW4(q[2]) + _POW4(q[3]);
    qval[2] = _POW6(q[0]) + _POW6(q[1]) + _POW6(q[2]) + _POW6(q[3]);
    qval[3] = _POW8(q[0]) + _POW8(q[1]) + _POW8(q[2]) + _POW8(q[3]);

    pi[2*ix  ] = 0.;
    pi[2*ix+1] = 0.;

    if(proj_type==0) {
/*      fprintf(stdout, "# using all 4x4 mu-nu-combinations\n"); */
      if(read_pimn0) {
        for(mu=0; mu<4; mu++) {
        for(nu=0; nu<4; nu++) {
          pi[2*ix  ] += ( q[mu]*q[nu] - q2*(double)(mu==nu) ) * (pimn[_GWI(4*mu+nu,ix,VOLUME)  ] - pimn0_value[2*(4*mu+nu)  ]);
          pi[2*ix+1] += ( q[mu]*q[nu] - q2*(double)(mu==nu) ) * (pimn[_GWI(4*mu+nu,ix,VOLUME)+1] - pimn0_value[2*(4*mu+nu)+1]);
        }}
      } else {
        for(mu=0; mu<4; mu++) {
        for(nu=0; nu<4; nu++) {
          pi[2*ix  ] += ( q[mu]*q[nu] - q2*(double)(mu==nu) ) * pimn[_GWI(4*mu+nu,ix,VOLUME)  ];
          pi[2*ix+1] += ( q[mu]*q[nu] - q2*(double)(mu==nu) ) * pimn[_GWI(4*mu+nu,ix,VOLUME)+1];
        }}
      }
      if(q2==0.) {
        pi[2*ix  ] = 0.;
        pi[2*ix+1] = 0.;
      }
      else {
        pi[2*ix  ] /= 3. * q2*q2; 
        pi[2*ix+1] /= 3. * q2*q2;
      }

    } else if(proj_type==1) {
      fprintf(stdout, "# using only 4 mu==nu-combinations\n");
      if(read_pimn0) {
        for(mu=0; mu<4; mu++) {
          pi[2*ix  ] += (pimn[_GWI(5*mu,ix,VOLUME)  ] - pimn0_value[10*mu  ]);
          pi[2*ix+1] += (pimn[_GWI(5*mu,ix,VOLUME)+1] - pimn0_value[10*mu+1]);
        }
      } else {
        for(mu=0; mu<4; mu++) {
          pi[2*ix  ] += pimn[_GWI(5*mu,ix,VOLUME)  ];
          pi[2*ix+1] += pimn[_GWI(5*mu,ix,VOLUME)+1];
        }
      }
      q2 = 3. * (q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3] );

      if(q2==0.) {
        pi[2*ix  ] = 0.;
        pi[2*ix+1] = 0.;
      }
      else {
        pi[2*ix  ] /= -q2; 
        pi[2*ix+1] /= -q2;
      }
    } else if(proj_type==2) {
      fprintf(stdout, "# using only 3 mu==nu-combinations\n");
      if(read_pimn0) {
        for(mu=1; mu<4; mu++) {
          pi[2*ix  ] += (pimn[_GWI(5*mu,ix,VOLUME)  ] - pimn0_value[10*mu  ]);
          pi[2*ix+1] += (pimn[_GWI(5*mu,ix,VOLUME)+1] - pimn0_value[10*mu+1]);
        }
      } else {
        for(mu=1; mu<4; mu++) {
          pi[2*ix  ] += pimn[_GWI(5*mu,ix,VOLUME)  ];
          pi[2*ix+1] += pimn[_GWI(5*mu,ix,VOLUME)+1];
        }
      }
      q2 = 3. * q[0]*q[0] + 2. * (q[1]*q[1] + q[2]*q[2] + q[3]*q[3] );

      if(q2==0.) {
        pi[2*ix  ] = 0.;
        pi[2*ix+1] = 0.;
      }
      else {
        pi[2*ix  ] /= -q2; 
        pi[2*ix+1] /= -q2;
      }
    }

    fprintf(stdout, "%3d%3d%3d%3d%16.7e%16.7e%16.7e%16.7e%16.7e%16.7e\n", x0, x1, x2, x3,
        qval[0], qval[1], qval[2], qval[3], pi[2*ix], pi[2*ix+1]);
  }}}}

  //for(x0=0; x0<T; x0++) {
  //for(x1=0; x1<LX; x1++) {
  //for(x2=0; x2<LY; x2++) {
  //for(x3=0; x3<LZ; x3++) {
  //  ix = g_ipt[x0][x1][x2][x3];
  //  fprintf(stdout, "%3d%3d%3d%3d\t(%25.16e + %25.16e*1.i)\n", x0, x1, x2, x3, pi[2*ix], pi[2*ix+1]);
  //}}}}


  fprintf(stdout, "\n# [analyse_piq] finished calculating pi\n");

/************************************************
 * mode 99: only write full q-dep pi
 ************************************************/
if(mode== 99) {
  sprintf(filename, "pi.%.2d.%.4d", 99, Nconf);
  if( (ofs=fopen(filename, "w")) == (FILE*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on opening file %s\n", filename);
    return(110);
  }
  for(i=0; i<VOLUME; i++) {
   fprintf(ofs, "%25.16e\t%25.16e\n", pi[2*i], pi[2*i+1]);
  }
  fclose(ofs); 
}

/************************************************
 * mode 1: average qhat2 orbits
 ************************************************/
if(mode==0 || mode==1) {

   fprintf(stdout, "\n# [analyse_piq] averaging over qhat2-orbits\n");

  if( (piavg = (double*)malloc(2*qhat2count*sizeof(double))) == (double*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on using malloc\n");
    return(108);
  }
  if( (picount = (int*)malloc(qhat2count*sizeof(int))) == (int*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on using malloc\n");
    return(109);
  }
  for(i=0; i<2*qhat2count; i++) piavg[i]   = 0.;
  for(i=0; i<  qhat2count; i++) picount[i] = 0;
  for(ix=0; ix<VOLUME; ix++) {
    piavg[2*qhat2id[ix]  ] += pi[2*ix  ];
    piavg[2*qhat2id[ix]+1] += pi[2*ix+1];
    picount[qhat2id[ix]]++;
  }
  for(i=0; i<  qhat2count; i++) {
    piavg[2*i  ] /= picount[i];
    piavg[2*i+1] /= picount[i];
  }

  sprintf(filename, "pi%1d.%.2d.%.4d", proj_type, 1, Nconf);
  if( (ofs=fopen(filename, "w")) == (FILE*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on opening file %s\n", filename);
    return(110);
  }
  for(i=0; i<  qhat2count; i++) {
   if(qhat2list[i] > _Q2EPS) {
     fprintf(ofs, "%21.12e\t%25.16e%25.16e%6d\n", qhat2list[i], 
       piavg[2*i], piavg[2*i+1], picount[i]);
     }
  }
  fclose(ofs); 
  free(piavg);
  free(picount);

}  // of if mode = 0/1

/************************************************
 * mode 2: qhat2 orbits with cylinder / cone cut
 ************************************************/
if(mode==0 || mode==2) {
  /*********************
   * apply cuts 
   *********************/
  for(ix=0; ix<VOLUME; ix++) workid[ix]=qhat2id[ix];

  if( make_cutid_list(workid, g_cutdir, g_cutradius, g_cutangle) != 0 ) return(125);

  /* average over qhat2-orbits */
  if( (piavg = (double*)malloc(2*qhat2count*sizeof(double))) == (double*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on using malloc\n");
    return(111);
  }
  if( (picount = (int*)malloc(qhat2count*sizeof(int))) == (int*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on using malloc\n");
    return(112);
  }
  for(i=0; i<2*qhat2count; i++) piavg[i]   = 0.;
  for(i=0; i<  qhat2count; i++) picount[i] = 0;
  for(ix=0; ix<VOLUME; ix++) {
    if(workid[ix]!=-1) {
      piavg[2*workid[ix]  ] += pi[2*ix  ];
      piavg[2*workid[ix]+1] += pi[2*ix+1];
      picount[workid[ix]]++;
    }
  }
  for(i=0; i<  qhat2count; i++) {
    if(picount[i]>0) {
      piavg[2*i  ] /= picount[i];
      piavg[2*i+1] /= picount[i];
    }
  }

  sprintf(filename, "pi%1d.%.2d.%.4d", proj_type, 2, Nconf);
  if( (ofs=fopen(filename, "w")) == (FILE*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on opening file %s\n", filename);
    return(113);
  }
  for(i=0; i<  qhat2count; i++) {
   if(picount[i]>0  && qhat2list[i]>_Q2EPS)
     fprintf(ofs, "%21.12e\t%25.16e%25.16e%6d\n", qhat2list[i], 
       piavg[2*i], piavg[2*i+1], picount[i]);
  }
  fclose(ofs);
  free(piavg);
  free(picount);

  sprintf(filename, "pi%1d.%.2d.%.4d.info", proj_type, 2, Nconf);
  if( (ofs=fopen(filename, "w")) == (FILE*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on opening file %s\n", filename);
    return(114);
  }
  fprintf(ofs, "Nconf:\t%.4d\navg:\t%s\nradius:\t%12.7e\nangle:\t%12.7e\n"\
               "dir:\t%3d%3d%3d%3d\n", Nconf, "qhat2", g_cutradius, g_cutangle, 
               g_cutdir[0], g_cutdir[1], g_cutdir[2], g_cutdir[3]);
  fclose(ofs);

}  // of mode = 0/2 

/**************************
 * mode 4: H4 orbits
 **************************/
if(mode==0 || mode==4) {

  for(ix=0; ix<VOLUME; ix++) workid[ix]=h4_id[ix];
  if( make_cutid_list(workid, g_cutdir, g_cutradius, g_cutangle) != 0 ) return(125);

  /**************************
   * average over orbits 
   **************************/
  if( (piavg = (double*)malloc(2 * h4_nc * sizeof(double))) == (double*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on using malloc\n");
    return(111);
  }
  if( (picount = (int*)malloc(h4_nc * sizeof(int))) == (int*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on using malloc\n");
    return(112);
  }
  for(i=0; i<2*h4_nc; i++) piavg[i] = 0.;
  for(i=0; i<h4_nc; i++) picount[i] = 0;

  for(ix=0; ix<VOLUME; ix++) {
    if(workid[ix] != -1) {
      piavg[2*workid[ix]  ] += pi[2*ix  ];
      piavg[2*workid[ix]+1] += pi[2*ix+1];
      picount[workid[ix]]++;
    }
  } 
  for(ix=0; ix<h4_nc; ix++) {
    if(picount[ix]>0) {
      piavg[2*ix  ] /= picount[ix];
      piavg[2*ix+1] /= picount[ix];
    }
    else {
      piavg[2*ix  ] = 0.;
      piavg[2*ix+1] = 0.;
    }
  }

  sprintf(filename, "pi%1d.%.2d.%.4d", proj_type, 4, Nconf);
  if( (ofs=fopen(filename, "w")) == (FILE*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on opening file %s\n", filename);
    return(128);
  }
  for(i=0; i<h4_nc; i++) {
   if(picount[i]>0 && h4_val[0][i]>_Q2EPS)
     fprintf(ofs, "%21.12e%21.12e%21.12e%21.12e%25.16e%25.16e%6d\n", \
       h4_val[0][i], h4_val[1][i], h4_val[2][i], h4_val[3][i],       \
       piavg[2*i], piavg[2*i+1], picount[i]);
  }
  fclose(ofs);

  free(piavg);
  free(picount);

  sprintf(filename, "pi%1d.%.2d.%.4d.info", proj_type, 4, Nconf);
  if( (ofs=fopen(filename, "w")) == (FILE*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on opening file %s\n", filename);
    return(129);
  }
  fprintf(ofs, "Nconf:\t%.4d\navg:\t%s\nradius:\t%12.7e\nangle:\t%12.7e\n"\
               "dir:\t%3d%3d%3d%3d\n", Nconf, "h4", g_cutradius, g_cutangle,
               g_cutdir[0], g_cutdir[1], g_cutdir[2], g_cutdir[3]);
  fclose(ofs);
 
}  // of mode = 0/4
  
/**************************
 * mode 3: H3 orbits
 **************************/
if(mode==0 || mode==3) {

  for(ix=0; ix<VOLUME; ix++) workid[ix]=h3_id[ix];
  if( make_cutid_list(workid, g_cutdir, g_cutradius, g_cutangle) != 0 ) return(125);

  /**************************
   * average over orbits 
   **************************/
  if( (piavg = (double*)malloc(2 * h3_nc * sizeof(double))) == (double*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on using malloc\n");
    return(111);
  }
  if( (picount = (int*)malloc(h3_nc * sizeof(int))) == (int*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on using malloc\n");
    return(112);
  }
  for(i=0; i<2*h3_nc; i++) piavg[i] = 0.;
  for(i=0; i<h3_nc; i++) picount[i] = 0;

  for(ix=0; ix<VOLUME; ix++) {
    if(workid[ix] != -1) {
      piavg[2*workid[ix]  ] += pi[2*ix  ];
      piavg[2*workid[ix]+1] += pi[2*ix+1];
      picount[workid[ix]]++;
    }
  } 
  for(ix=0; ix<h3_nc; ix++) {
    if(picount[ix]>0) {
      piavg[2*ix  ] /= picount[ix];
      piavg[2*ix+1] /= picount[ix];
    }
    else {
      piavg[2*ix  ] = 0.;
      piavg[2*ix+1] = 0.;
    }
  }

  sprintf(filename, "pi%1d.%.2d.%.4d", proj_type, 3, Nconf);
  if( (ofs=fopen(filename, "w")) == (FILE*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on opening file %s\n", filename);
    return(128);
  }
  for(i=0; i<h3_nc; i++) {
   if(picount[i]>0 && h3_val[0][i]>_Q2EPS)
     fprintf(ofs, "%21.12e%21.12e%21.12e%21.12e%25.16e%25.16e%6d\n", \
       h3_val[0][i], h3_val[1][i], h3_val[2][i], h3_val[3][i],       \
       piavg[2*i], piavg[2*i+1], picount[i]);
  }
  fclose(ofs);

  free(piavg);
  free(picount);

  sprintf(filename, "pi%1d.%.2d.%.4d.info", proj_type, 3, Nconf);
  if( (ofs=fopen(filename, "w")) == (FILE*)NULL ) {
    fprintf(stderr, "\n[analyse_piq] Error on opening file %s\n", filename);
    return(129);
  }
  fprintf(ofs, "Nconf:\t%.4d\navg:\t%s\nradius:\t%12.7e\nangle:\t%12.7e\n"\
               "dir:\t%3d%3d%3d%3d\n", Nconf, "h3", g_cutradius, g_cutangle,
               g_cutdir[0], g_cutdir[1], g_cutdir[2], g_cutdir[3]);
  fclose(ofs);
 
}  // of mode == 0/3

// } // of loop on iconf


  /**************************
   * free and finalize
   **************************/

  if(h3_val != (double**)NULL) {
    if(*h3_val != (double*)NULL) free(*h3_val);
    free(h3_val);
  }
  if(h4_val != (double**)NULL) {
    if(*h4_val != (double*)NULL) free(*h4_val);
    free(h4_val);
  }
  if(h3_count != (int*)NULL) free(h3_count);
  if(h4_count != (int*)NULL) free(h4_count);
  if(h3_id != (int*)NULL) free(h3_id);
  if(h4_id != (int*)NULL) free(h4_id);
  if(pimn      != NULL) free(pimn);
  if(pi        != NULL) free(pi);
  if(q2id      != NULL) free(q2id);
  if(qhat2id   != NULL) free(qhat2id);
  if(q2list    != NULL) free(q2list);
  if(qhat2list != NULL) free(qhat2list);

  if(g_cart_id == 0) {
    g_the_time = time(NULL);
    fprintf(stdout, "\n# [analyse_piq] %s# [analyse_piq] end of run\n", ctime(&g_the_time));
    fprintf(stderr, "\n# [analyse_piq] %s# [analyse_piq] end of run\n", ctime(&g_the_time));
  }

  return(0);
}
