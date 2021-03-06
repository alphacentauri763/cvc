/****************************************************
 * check_hpe5.c
 *
 * Wed Nov 11 09:00:53 CET 2009
 *
 * TODO: 
 * DONE:
 * CHANGES:
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
#include "propagator_io.h"
#include "Q_phi.h"

void usage() {
  fprintf(stdout, "Code to perform light neutral contractions\n");
  fprintf(stdout, "Usage:    [options]\n");
  fprintf(stdout, "Options: -v verbose\n");
  fprintf(stdout, "         -g apply a random gauge transformation\n");
  fprintf(stdout, "         -f input filename [default cvc.input]\n");
#ifdef MPI
  MPI_Abort(MPI_COMM_WORLD, 1);
  MPI_Finalize();
#endif
  exit(0);
}


int main(int argc, char **argv) {
  
  int c, i, mu, nu;
  int count        = 0;
  int filename_set = 0;
  int dims[4]      = {0,0,0,0};
  int l_LX_at, l_LXstart_at;
  int x0, x1, x2, x3, ix, iix;
  int dxm[4], dxn[4], ixpm, ixpn;
  int sid;
  double disc[16];
  double *work = (double*)NULL;
  double q[4], fnorm;
  int verbose = 0;
  int do_gt   = 0;
  char filename[100];
  double ratime, retime;
  double plaq, _2kappamu, hpe3_coeff, onepmutilde2, mutilde2;
  double spinor1[24], spinor2[24], U_[18], U1_[18], U2_[18];
  double *gauge_trafo=(double*)NULL;
  complex w, w1, w2, *cp1, *cp2, *cp3;
  FILE *ofs;

#ifdef MPI
  int *status;
#endif

#ifdef MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "h?vgf:")) != -1) {
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
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  /* set the default values */
  set_default_input_values();
  if(filename_set==0) strcpy(filename, "cvc.input");

  /* read the input file */
  read_input(filename);

  /* some checks on the input data */
  if((T_global == 0) || (LX==0) || (LY==0) || (LZ==0)) {
    if(g_proc_id==0) fprintf(stdout, "T and L's must be set\n");
    usage();
  }
  if(g_kappa == 0.) {
    if(g_proc_id==0) fprintf(stdout, "kappa should be > 0.n");
    usage();
  }

  /* initialize MPI parameters */
  mpi_init(argc, argv);
#ifdef MPI
  if((status = (int*)calloc(g_nproc, sizeof(int))) == (int*)NULL) {
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
    exit(7);
  }
#endif

  /* initialize */
  dims[0]=T_global; dims[1]=LX; dims[2]=LY; dims[3]=LZ;
#ifndef MPI
  T            = T_global;
  Tstart       = 0;
  l_LX_at      = LX;
  l_LXstart_at = 0;
#endif
  fprintf(stdout, "# [%2d] parameters:\n"\
                  "# [%2d] T            = %3d\n"\
		  "# [%2d] Tstart       = %3d\n"\
		  "# [%2d] l_LX_at      = %3d\n"\
		  "# [%2d] l_LXstart_at = %3d\n",
		  g_cart_id, g_cart_id, T, g_cart_id, Tstart, g_cart_id, l_LX_at,
		  g_cart_id, l_LXstart_at);
#ifdef MPI
  if(T==0) {
    fprintf(stderr, "[%2d] local T is zero; exit\n", g_cart_id);
    MPI_Abort(MPI_COMM_WORLD, 1);
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

  /* read the gauge field */
  alloc_gauge_field(&g_gauge_field, VOLUMEPLUSRAND);
  sprintf(filename, "%s.%.4d", gaugefilename_prefix, Nconf);
  if(g_cart_id==0) fprintf(stdout, "reading gauge field from file %s\n", filename);
  read_lime_gauge_field_doubleprec(filename);
#ifdef MPI
  xchange_gauge();
#endif

  /* measure the plaquette */
  plaquette(&plaq);
  if(g_cart_id==0) fprintf(stdout, "measured plaquette value: %25.16e\n", plaq);

  if(do_gt==1) {
    /***********************************
     * initialize gauge transformation
     ***********************************/
    init_gauge_trafo(&gauge_trafo, 1.);
    apply_gt_gauge(gauge_trafo);
    plaquette(&plaq);
    if(g_cart_id==0) fprintf(stdout, "measured plaquette value after gauge trafo: %25.16e\n", plaq);
  }

  /****************************************
   * allocate memory for the spinor fields
   ****************************************/
  no_fields = 3;
  g_spinor_field = (double**)calloc(no_fields, sizeof(double*));
  for(i=0; i<no_fields; i++) alloc_spinor_field(&g_spinor_field[i], VOLUMEPLUSRAND);

  
  for(ix=0; ix<16; ix++) disc[ix] = 0.;

  /************************************************
   * HPE: calculate coeff. of 3rd order term
   ************************************************/
  _2kappamu    = 2. * g_kappa * g_mu;
  onepmutilde2 = 1. + _2kappamu * _2kappamu;
  mutilde2     = _2kappamu * _2kappamu;

  hpe3_coeff   = 16. * g_kappa*g_kappa*g_kappa*g_kappa * (1. + 6. * mutilde2 + mutilde2*mutilde2) / onepmutilde2 / onepmutilde2 / onepmutilde2 / onepmutilde2;
  fprintf(stdout, "hpe3_coeff = %25.16e\n", hpe3_coeff);

  /***********************************************
   * from here: everything as before except for
   *            the change BH5 --> BH7
   *
   * start loop on source id.s 
   ***********************************************/
  for(sid=0; sid<12; sid++) {

    for(ix=0; ix<VOLUME; ix++) {
      _fv_eq_zero(g_spinor_field[0]+_GSI(ix));
      _fv_eq_zero(g_spinor_field[1]+_GSI(ix));
    }
    g_spinor_field[0][2*sid] = 1.;
    g_spinor_field[1][2*sid] = 1.;

    /************************************************
     * HPE: apply (BH)^3 B
     ************************************************/
    mul_one_pm_imu_inv (g_spinor_field[1], +1., VOLUME);
    BH3(g_spinor_field[2], g_spinor_field[1]);

    /************************************************
     * add new contractions to (existing) disc 
     ************************************************/
    for(mu=0; mu<4; mu++) { /* loop on Lorentz index of the current */
      ix=0;
        _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix, mu)], &co_phase_up[mu]);

        /* first contribution */
        _fv_eq_cm_ti_fv(spinor1, U_, &g_spinor_field[2][_GSI(g_iup[ix][mu])]);
	_fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	_fv_mi_eq_fv(spinor2, spinor1);
	_co_eq_fv_dag_ti_fv(&w, &g_spinor_field[0][_GSI(ix)], spinor2);
	disc[2*mu  ] -= g_kappa * w.re;
	disc[2*mu+1] -= g_kappa * w.im;

        /* second contribution */
	_fv_eq_cm_dag_ti_fv(spinor1, U_, &g_spinor_field[2][_GSI(ix)]);
	_fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	_fv_pl_eq_fv(spinor2, spinor1);
	_co_eq_fv_dag_ti_fv(&w, &g_spinor_field[0][_GSI(g_iup[ix][mu])], spinor2);
	disc[8+2*mu  ] -= g_kappa * w.re;
	disc[8+2*mu+1] -= g_kappa * w.im;

    }    /* of mu */
  }

for(nu=0; nu<4; nu++) {
  for(sid=0; sid<12; sid++) {

    for(ix=0; ix<VOLUME; ix++) {
      _fv_eq_zero(g_spinor_field[0]+_GSI(ix));
      _fv_eq_zero(g_spinor_field[1]+_GSI(ix));
    }
    g_spinor_field[0][_GSI(g_iup[0][nu]) + 2*sid] = 1.;
    g_spinor_field[1][_GSI(g_iup[0][nu]) + 2*sid] = 1.;

    /************************************************
     * HPE: apply (BH)^3 B
     ************************************************/
    mul_one_pm_imu_inv (g_spinor_field[1], +1., VOLUME);
    BH3(g_spinor_field[2], g_spinor_field[1]);

    /************************************************
     * add new contractions to (existing) disc 
     ************************************************/
    for(mu=0; mu<4; mu++) { /* loop on Lorentz index of the current */
      ix=0;
        _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix, mu)], &co_phase_up[mu]);

        /* first contribution */
        _fv_eq_cm_ti_fv(spinor1, U_, &g_spinor_field[2][_GSI(g_iup[ix][mu])]);
	_fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	_fv_mi_eq_fv(spinor2, spinor1);
	_co_eq_fv_dag_ti_fv(&w, &g_spinor_field[0][_GSI(ix)], spinor2);
	disc[2*mu  ] -= g_kappa * w.re;
	disc[2*mu+1] -= g_kappa * w.im;

        /* second contribution */
	_fv_eq_cm_dag_ti_fv(spinor1, U_, &g_spinor_field[2][_GSI(ix)]);
	_fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	_fv_pl_eq_fv(spinor2, spinor1);
	_co_eq_fv_dag_ti_fv(&w, &g_spinor_field[0][_GSI(g_iup[ix][mu])], spinor2);
	disc[8+2*mu  ] -= g_kappa * w.re;
	disc[8+2*mu+1] -= g_kappa * w.im;

    }    /* of mu */
  }
}

  /************************************************
   * save results for count = multiple of Nsave 
   ************************************************/

  sprintf(filename, "hpe3_coeff_ix0.%.4d", Nconf);
  ofs = fopen(filename, "w");
  for(mu=0; mu<4; mu++) {
    fprintf(ofs, "%3d%25.16e%25.16e%25.16e%25.16e%25.16e%25.16e\n", mu, disc[2*mu], disc[2*mu+1], 
      disc[8+2*mu], disc[8+2*mu+1], disc[2*mu]+disc[8+2*mu], disc[2*mu+1]+disc[8+2*mu+1]);
  }
  fclose(ofs);

  /***********************************************
   * free the allocated memory, finalize 
   ***********************************************/
  free(g_gauge_field);
  for(i=0; i<no_fields; i++) free(g_spinor_field[i]);
  free(g_spinor_field);
  free_geometry();

  return(0);

}
