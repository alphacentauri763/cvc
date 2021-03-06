/****************************************************
  
 * cvc_lvc_exact_lowmem_pspace.c
 *
 * Sat Nov 26 11:56:30 CET 2011
 *
 * PURPOSE:
 * - adapted from avc_exact2_lowmem_pspace
 * - works with output of cvc_lvc_exact_lowmme_xspace
 *
 * DONE:
 * TODO:
 ****************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef MPI
#  include <mpi.h>
#endif
#ifdef OPENMP
#  include <omp.h>
#endif
#include "ifftw.h"
#include <getopt.h>

#define MAIN_PROGRAM

#include "cvc_complex.h"
#include "cvc_linalg.h"
#include "global.h"
#include "cvc_geometry.h"
#include "cvc_utils.h"
#include "mpi_init.h"
#include "io.h"
#include "propagator_io.h"
#include "Q_phi.h"
#include "read_input_parser.h"

void usage() {
  fprintf(stdout, "Usage:    [options]\n");
  fprintf(stdout, "Options: -v verbose\n");
  fprintf(stdout, "         -g apply a random gauge transformation\n");
  fprintf(stdout, "         -f input filename [default cvc.input]\n");
#ifdef MPI
  MPI_Abort(MPI_COMM_WORLD, 0);
  MPI_Finalize();
#endif
  exit(0);
}


int main(int argc, char **argv) {
  
  int c, i, j, mu, nu, ir, is, ia, ib, imunu, status;
  int filename_set = 0;
  int dims[4]      = {0,0,0,0};
  int l_LX_at, l_LXstart_at;
  int source_location, have_source_flag = 0;
  int x0, x1, x2, x3, ix;
  int sx0, sx1, sx2, sx3;
  int isimag[4];
  int gperm[5][4], gperm2[4][4];
  int check_position_space_WI=0, check_momentum_space_WI=0;
  int num_threads = 1, nthreads=-1, threadid=-1;
  int exitstatus;
  int write_ascii=0;
  int mms = 0, mass_id = -1;
  int outfile_prefix_set = 0;
  double gperm_sign[5][4], gperm2_sign[4][4];
  double *conn = (double*)NULL;
  double phase[4];
  double *work=NULL;
  int verbose = 0;
  int do_gt   = 0;
  char filename[100], contype[400], outfile_prefix[400];
  double ratime, retime;
  double plaq;
  double spinor1[24], spinor2[24], U_[18];
  double *gauge_trafo=(double*)NULL;
  double *phi=NULL, *chi=NULL;
  complex w, w1;
  double Usourcebuff[72], *Usource[4];
  FILE *ofs;

  fftw_complex *in=(fftw_complex*)NULL;

#ifdef MPI
  fftwnd_mpi_plan plan_p;
  int *status;
#else
  fftwnd_plan plan_p;
#endif

#ifdef MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "wWah?vgf:t:m:o:")) != -1) {
    switch (c) {
    case 'v':
      verbose = 1;
      break;
    case 'g':
      do_gt = 1;
      break;
    case 'f':
      strcpy(filename, optarg);
      filename_set=1;
      break;
    case 'w':
      check_position_space_WI = 1;
      fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] will check Ward identity in position space\n");
      break;
    case 'W':
      check_momentum_space_WI = 1;
      fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] will check Ward identity in momentum space\n");
      break;
    case 't':
      num_threads = atoi(optarg);
      fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] will use %d threads in spacetime loops\n", num_threads);
      break;
    case 'a':
      write_ascii = 1;
      fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] will write data in ASCII format too\n");
      break;
    case 'm':
      mms = 1;
      mass_id = atoi(optarg);
      fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] will read propagators in MMS format with mass id $d\n", mass_id);
      break;
    case 'o':
      strcpy(outfile_prefix, optarg);
      fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] will use prefix $s for output filenames\n", outfile_prefix);
      outfile_prefix_set = 1;
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  if(g_cart_id==0) {
    g_the_time = time(NULL);
    fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] using global time stamp %s", ctime(&g_the_time));
  }

#if (defined PARALLELTX) || (defined PARALLELTXY)
  fprintf(stderr, "\nError, no implementation for this domain decomposition pattern\n");
  exit(123);
#endif


  /*********************************
   * set number of openmp threads
   *********************************/
#ifdef OPENMP
  omp_set_num_threads(num_threads);
#endif

  /* set the default values */
  if(filename_set==0) strcpy(filename, "cvc.input");
  fprintf(stdout, "# Reading input from file %s\n", filename);
  read_input_parser(filename);

  /* some checks on the input data */
  if((T_global == 0) || (LX==0) || (LY==0) || (LZ==0)) {
    if(g_proc_id==0) fprintf(stderr, "\n[cvc_lvc_exact2_lowmem_pspace] T and L's must be set\n");
    usage();
  }
  if(g_kappa == 0.) {
    if(g_proc_id==0) fprintf(stderr, "\n[cvc_lvc_exact2_lowmem_pspace] kappa should be > 0.n");
    usage();
  }

  /* initialize MPI parameters */
  mpi_init(argc, argv);
#ifdef MPI
  if((status = (int*)calloc(g_nproc, sizeof(int))) == (int*)NULL) {
    MPI_Abort(MPI_COMM_WORLD, 7);
    MPI_Finalize();
    exit(7);
  }
#endif

  /*******************************
   * initialize fftw
   *******************************/
#ifdef OPENMP
  exitstatus = fftw_threads_init();
  if(exitstatus != 0) {
    fprintf(stderr, "\n[cvc_lvc_exact2_lowmem_pspace] Error from fftw_init_threads; status was %d\n", exitstatus);
    exit(120);
  }
#endif


  dims[0]=T_global; dims[1]=LX; dims[2]=LY; dims[3]=LZ;
#ifdef MPI
  plan_p = fftwnd_mpi_create_plan(g_cart_grid, 4, dims, FFTW_BACKWARD, FFTW_MEASURE);
  fftwnd_mpi_local_sizes(plan_p, &T, &Tstart, &l_LX_at, &l_LXstart_at, &FFTW_LOC_VOLUME);
#else
  plan_p = fftwnd_create_plan(4, dims, FFTW_BACKWARD, FFTW_MEASURE | FFTW_IN_PLACE);
  T            = T_global;
  Tstart       = 0;
  l_LX_at      = LX;
  l_LXstart_at = 0;
  FFTW_LOC_VOLUME = T*LX*LY*LZ;
#endif
  fprintf(stdout, "# [%2d] fftw parameters:\n"\
                  "# [%2d] T            = %3d\n"\
		  "# [%2d] Tstart       = %3d\n"\
		  "# [%2d] l_LX_at      = %3d\n"\
		  "# [%2d] l_LXstart_at = %3d\n"\
		  "# [%2d] FFTW_LOC_VOLUME = %3d\n", 
		  g_cart_id, g_cart_id, T, g_cart_id, Tstart, g_cart_id, l_LX_at,
		  g_cart_id, l_LXstart_at, g_cart_id, FFTW_LOC_VOLUME);

#ifdef MPI
  if(T==0) {
    fprintf(stderr, "[%2d] local T is zero; exit\n", g_cart_id);
    MPI_Abort(MPI_COMM_WORLD, 2);
    MPI_Finalize();
    exit(2);
  }
#endif

  if(init_geometry() != 0) {
    fprintf(stderr, "ERROR from init_geometry\n");
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
#endif
    exit(1);
  }

  geometry();

  /* allocate memory for the contractions */
  conn = (double*)calloc(2 * 16 * VOLUME, sizeof(double));
  if( conn==(double*)NULL ) {
    fprintf(stderr, "could not allocate memory for contr. fields\n");
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 3);
    MPI_Finalize();
#endif
    exit(3);
  }

  /***********************************************************
   * prepare Fourier transformation arrays
   ***********************************************************/
  in  = (fftw_complex*)malloc(FFTW_LOC_VOLUME*sizeof(fftw_complex));
  if(in==(fftw_complex*)NULL) {    
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 4);
    MPI_Finalize();
#endif
    exit(4);
  }


  /***********************************************************
   * determine source coordinates, find out, if source_location is in this process
   ***********************************************************/
  have_source_flag = (int)(g_source_location/(LX*LY*LZ)>=Tstart && g_source_location/(LX*LY*LZ)<(Tstart+T));
  if(have_source_flag==1) fprintf(stdout, "process %2d has source location\n", g_cart_id);
  sx0 = g_source_location/(LX*LY*LZ)-Tstart;
  sx1 = (g_source_location%(LX*LY*LZ)) / (LY*LZ);
  sx2 = (g_source_location%(LY*LZ)) / LZ;
  sx3 = (g_source_location%LZ);

#ifdef MPI
      ratime = MPI_Wtime();
#else
      ratime = (double)clock() / CLOCKS_PER_SEC;
#endif

  /* read the position space contractions */
#ifdef MPI
  ratime = MPI_Wtime();
#else
  ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(outfile_prefix_set) {
    sprintf(filename, "%s/%s_x.%.4d.t%.2dx%.2dy%.2dz%.2d", outfile_prefix, filename_prefix, Nconf, sx0, sx1, sx2, sx3);
  } else {
    sprintf(filename, "%s_x.%.4d.t%.2dx%.2dy%.2dz%.2d", filename_prefix, Nconf, sx0, sx1, sx2, sx3);
  }
  status = read_lime_contraction(conn, filename, 16, 0);
  if( status != 0 ) {
    fprintf(stderr, "[] Error, could not read from file %s\n", filename);
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 112);
    MPI_Finalize();
#endif
    exit(112);
  }
#ifdef MPI
  retime = MPI_Wtime();
#else
  retime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(g_cart_id==0) fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] time to read contraction data: %e seconds\n", retime-ratime);

#ifndef MPI
  /* check the Ward identity in position space */
  if(check_position_space_WI) {
    sprintf(filename, "WI_X.%.4d", Nconf);
    ofs = fopen(filename,"w");
    fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] checking Ward identity in position space ...\n");
    for(x0=0; x0<T;  x0++) {
    for(x1=0; x1<LX; x1++) {
    for(x2=0; x2<LY; x2++) {
    for(x3=0; x3<LZ; x3++) {
      fprintf(ofs, "# t=%2d x=%2d y=%2d z=%2d\n", x0, x1, x2, x3);
      ix=g_ipt[x0][x1][x2][x3];
      for(nu=0; nu<4; nu++) {
        w.re = conn[_GWI(4*0+nu,ix,VOLUME)] + conn[_GWI(4*1+nu,ix,VOLUME)]
             + conn[_GWI(4*2+nu,ix,VOLUME)] + conn[_GWI(4*3+nu,ix,VOLUME)]
	     - conn[_GWI(4*0+nu,g_idn[ix][0],VOLUME)] - conn[_GWI(4*1+nu,g_idn[ix][1],VOLUME)]
	     - conn[_GWI(4*2+nu,g_idn[ix][2],VOLUME)] - conn[_GWI(4*3+nu,g_idn[ix][3],VOLUME)];

        w.im = conn[_GWI(4*0+nu,ix,VOLUME)+1] + conn[_GWI(4*1+nu,ix,VOLUME)+1]
            + conn[_GWI(4*2+nu,ix,VOLUME)+1] + conn[_GWI(4*3+nu,ix,VOLUME)+1]
	    - conn[_GWI(4*0+nu,g_idn[ix][0],VOLUME)+1] - conn[_GWI(4*1+nu,g_idn[ix][1],VOLUME)+1]
	    - conn[_GWI(4*2+nu,g_idn[ix][2],VOLUME)+1] - conn[_GWI(4*3+nu,g_idn[ix][3],VOLUME)+1];
      
        fprintf(ofs, "\t%3d%25.16e%25.16e\n", nu, w.re, w.im);
      }
    }}}}
    fclose(ofs);
  }
#endif

  /*********************************************
   * Fourier transformation 
   *********************************************/
#ifdef MPI
  ratime = MPI_Wtime();
#else
  ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
  for(mu=0; mu<16; mu++) {
    memcpy((void*)in, (void*)&conn[_GWI(mu,0,VOLUME)], 2*VOLUME*sizeof(double));
#ifdef MPI
    fftwnd_mpi(plan_p, 1, in, NULL, FFTW_NORMAL_ORDER);
#else
#  ifdef OPENMP
    fftwnd_threads_one(num_threads, plan_p, in, NULL);
#  else
    fftwnd_one(plan_p, in, NULL);
#  endif
#endif
    memcpy((void*)&conn[_GWI(mu,0,VOLUME)], (void*)in, 2*VOLUME*sizeof(double));
  }

  /*****************************************
   * add phase factors
   *****************************************/
  for(mu=0; mu<4; mu++) {
    phi = conn + _GWI(5*mu,0,VOLUME);

    for(x0=0; x0<T; x0++) {
      phase[0] = 2. * (double)(Tstart+x0) * M_PI / (double)T_global;
    for(x1=0; x1<LX; x1++) {
      phase[1] = 2. * (double)(x1) * M_PI / (double)LX;
    for(x2=0; x2<LY; x2++) {
      phase[2] = 2. * (double)(x2) * M_PI / (double)LY;
    for(x3=0; x3<LZ; x3++) {
      phase[3] = 2. * (double)(x3) * M_PI / (double)LZ;
      ix = g_ipt[x0][x1][x2][x3];
      w.re =  cos( phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3 );
      w.im = -sin( phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3 );
      _co_eq_co_ti_co(&w1,(complex*)( phi+2*ix ), &w);
      phi[2*ix  ] = w1.re;
      phi[2*ix+1] = w1.im;
    }}}}
  }  /* of mu */

  for(mu=0; mu<3; mu++) {
  for(nu=mu+1; nu<4; nu++) {
    phi = conn + _GWI(4*mu+nu,0,VOLUME);
    chi = conn + _GWI(4*nu+mu,0,VOLUME);

    for(x0=0; x0<T; x0++) {
      phase[0] =  (double)(Tstart+x0) * M_PI / (double)T_global;
    for(x1=0; x1<LX; x1++) {
      phase[1] =  (double)(x1) * M_PI / (double)LX;
    for(x2=0; x2<LY; x2++) {
      phase[2] =  (double)(x2) * M_PI / (double)LY;
    for(x3=0; x3<LZ; x3++) {
      phase[3] =  (double)(x3) * M_PI / (double)LZ;
      ix = g_ipt[x0][x1][x2][x3];
      w.re =  cos( phase[mu] - phase[nu] - 2.*(phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3) );
      w.im =  sin( phase[mu] - phase[nu] - 2.*(phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3) );
      _co_eq_co_ti_co(&w1,(complex*)( phi+2*ix ), &w);
      phi[2*ix  ] = w1.re;
      phi[2*ix+1] = w1.im;

      w.re =  cos( phase[nu] - phase[mu] - 2.*(phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3) );
      w.im =  sin( phase[nu] - phase[mu] - 2.*(phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3) );
      _co_eq_co_ti_co(&w1,(complex*)( chi+2*ix ), &w);
      chi[2*ix  ] = w1.re;
      chi[2*ix+1] = w1.im;
    }}}}
  }}  /* of mu and nu */

#ifdef MPI
  retime = MPI_Wtime();
#else
  retime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(g_cart_id==0) fprintf(stdout, "Fourier transform in %e seconds\n", retime-ratime);

  /********************************
   * save momentum space results
   ********************************/
#ifdef MPI
  ratime = MPI_Wtime();
#else
  ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(outfile_prefix_set) {
    sprintf(filename, "%s/%s_p.%.4d", outfile_prefix, filename_prefix2, Nconf);
  } else {
    sprintf(filename, "%s_p.%.4d", filename_prefix2, Nconf);
  }
  sprintf(contype, "current - current correlator in momentum space, all 16 components");
  write_lime_contraction(conn, filename, 64, 16, contype, Nconf, 0);
#ifndef MPI
  if(write_ascii) {
    if(outfile_prefix_set) {
      sprintf(filename, "%s/%s_p.%.4d", outfile_prefix, filename_prefix2, Nconf);
    } else {
      sprintf(filename, "%s_p.%.4d.ascii", filename_prefix2, Nconf);
    }
    write_contraction(conn, (int*)NULL, filename, 16, 2, 0);
  }
#endif

#ifdef MPI
  retime = MPI_Wtime();
#else
  retime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(g_cart_id==0) fprintf(stdout, "saved momentum space results in %e seconds\n", retime-ratime);

  if(check_momentum_space_WI) {
#ifdef MPI
    sprintf(filename, "WI_P.%.4d.%.2d", Nconf, g_cart_id);
#else
    sprintf(filename, "WI_P.%.4d", Nconf);
#endif  
    ofs = fopen(filename,"w");
    if(g_cart_id == 0) fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] checking Ward identity in momentum space ...\n");
    for(x0=0; x0<T; x0++) {
      phase[0] = 2. * sin( (double)(Tstart+x0) * M_PI / (double)T_global );
    for(x1=0; x1<LX; x1++) {
      phase[1] = 2. * sin( (double)(x1) * M_PI / (double)LX );
    for(x2=0; x2<LY; x2++) {
      phase[2] = 2. * sin( (double)(x2) * M_PI / (double)LY );
    for(x3=0; x3<LZ; x3++) {
      phase[3] = 2. * sin( (double)(x3) * M_PI / (double)LZ );
      ix = g_ipt[x0][x1][x2][x3];
      fprintf(ofs, "# t=%2d x=%2d y=%2d z=%2d\n", x0, x1, x2, x3);
      for(nu=0;nu<4;nu++) {
        w.re = phase[0] * conn[_GWI(4*0+nu,ix,VOLUME)] + phase[1] * conn[_GWI(4*1+nu,ix,VOLUME)] 
             + phase[2] * conn[_GWI(4*2+nu,ix,VOLUME)] + phase[3] * conn[_GWI(4*3+nu,ix,VOLUME)];

        w.im = phase[0] * conn[_GWI(4*0+nu,ix,VOLUME)+1] + phase[1] * conn[_GWI(4*1+nu,ix,VOLUME)+1] 
             + phase[2] * conn[_GWI(4*2+nu,ix,VOLUME)+1] + phase[3] * conn[_GWI(4*3+nu,ix,VOLUME)+1];

        w1.re = phase[0] * conn[_GWI(4*nu+0,ix,VOLUME)] + phase[1] * conn[_GWI(4*nu+1,ix,VOLUME)] 
              + phase[2] * conn[_GWI(4*nu+2,ix,VOLUME)] + phase[3] * conn[_GWI(4*nu+3,ix,VOLUME)];

        w1.im = phase[0] * conn[_GWI(4*nu+0,ix,VOLUME)+1] + phase[1] * conn[_GWI(4*nu+1,ix,VOLUME)+1] 
              + phase[2] * conn[_GWI(4*nu+2,ix,VOLUME)+1] + phase[3] * conn[_GWI(4*nu+3,ix,VOLUME)+1];
        fprintf(ofs, "\t%d%25.16e%25.16e%25.16e%25.16e\n", nu, w.re, w.im, w1.re, w1.im);
      }
    }}}}
    fclose(ofs);
  }

  /****************************************
   * free the allocated memory, finalize
   ****************************************/
  free_geometry();
  fftw_free(in);
  free(conn);
#ifdef MPI
  fftwnd_mpi_destroy_plan(plan_p);
  free(status);
  MPI_Finalize();
#else
  fftwnd_destroy_plan(plan_p);
#endif

  if(g_cart_id==0) {
    g_the_time = time(NULL);
    fprintf(stdout, "\n# [cvc_lvc_exact2_lowmem_pspace] %s# [cvc_lvc_exact2_lowmem_pspace] end of run\n", ctime(&g_the_time));
    fprintf(stderr, "\n# [cvc_lvc_exact2_lowmem_pspace] %s# [cvc_lvc_exact2_lowmem_pspace] end of run\n", ctime(&g_the_time));
  }

  return(0);

}
