#include <stdio.h>
#include "fem.h"
#include <occa.hpp>

// switches b/w nodal and bernstein bases
#define USE_SLICE_LIFT 0 // switch on for faster behavior if N > 6
int ngeo, nvgeo, nfgeo; // number of geometric factors

// OCCA device
occa::device device;
occa::kernelInfo dgInfo;

// OCCA array for geometric factors
occa::memory c_vgeo;
occa::memory c_fgeo;

// OCCA array for index of positive trace variables wrt face nodes
occa::memory c_vmapP;
occa::memory c_Fmask;

// rhs for RK kernels
occa::memory c_rhsQ;
occa::memory c_resQ;

occa::memory c_rhsP;
occa::memory c_resP;

occa::memory c_Q;
occa::memory c_P;

// OCCA arrays for nodal derivative/lift matrices
occa::memory c_Dr;
occa::memory c_Ds;
occa::memory c_Dt;
occa::memory c_LIFT;

// OCCA arrays for projection w.r.t nodal basis
occa::memory c_Pq;
occa::memory c_Cq;
occa::memory c_Vq;

// OCCA arrays for projection w.r.t Bernstein basis
occa::memory c_PqB;
occa::memory c_VqB;

// OCCA arrays for projected c^2
occa::memory c_CB;

//OCCA arrays for Bern multiplication
occa::memory c_col_val;
occa::memory c_col_id;
occa::memory c_L_id;

//OCCA arrays for Bern projection
occa::memory c_ENMT_val;
occa::memory c_ENM_val;
occa::memory c_ENMT_id;
occa::memory c_ENM_id;
occa::memory c_co;
occa::memory c_E;
occa::memory c_ENMT_index;


// Bernstein derivative operators
occa::memory c_Dvals4;
occa::memory c_D_ids1;
occa::memory c_D_ids2;
occa::memory c_D_ids3;
occa::memory c_D_ids4;

// Bernstein LIFT decomposition
occa::memory c_EEL_vals;
occa::memory c_EEL_ids;
occa::memory c_L0_vals;
occa::memory c_L0_ids;
occa::memory c_cEL;
occa::memory c_slice_ids; // permutation of rows

// nodal kernels
occa::kernel rk_volume;
occa::kernel rk_surface;

// bernstein kernels
occa::kernel rk_volume_bern;
occa::kernel rk_surface_bern;

// used for both nodal and Bernstein
occa::kernel rk_update;

// kernels added by myself
occa::kernel rk_update_BB_WADG;
occa::kernel rk_update_WADG;

// block sizes for optimization of kernels
int KblkV, KblkS, KblkU;

// runtime counters
double timeV = 0.0, timeS = 0.0, timeU=0.0, timeQ = 0.0, timeQf = 0.0, timeM = 0.0, timeP = 0.0;

template <typename T>
void diagnose_array(const char *message, occa::memory &c_a, int N){

  device.finish();
  T *h_a = (T*) calloc(N, sizeof(T));
  c_a.copyTo(h_a, N*sizeof(T));

  dfloat suma = 0;
  for(int n=0;n<N;++n){
    suma += h_a[n];
  }
  printf("%s: sum = %17.15g\n", message, suma);
  free(h_a);
}


// set occa array:: cast to dfloat.
void setOccaArray(MatrixXd A, occa::memory &c_A){
  int r = A.rows();
  int c = A.cols();
  dfloat *f_A = (dfloat*)malloc(r*c*sizeof(dfloat));
  Map<MatrixXdf >(f_A,r,c) = A.cast<dfloat>();
  c_A = device.malloc(r*c*sizeof(dfloat),f_A);
  free(f_A);
}

// set occa array:: cast to dfloat
void setOccaIntArray(MatrixXi A, occa::memory &c_A){
  int r = A.rows();
  int c = A.cols();
  int *f_A = (int*)malloc(r*c*sizeof(int));
  Map<MatrixXi >(f_A,r,c) = A;
  c_A = device.malloc(r*c*sizeof(int),f_A);
  free(f_A);
}


dfloat WaveInitOCCA3d(Mesh *mesh, int KblkVin, int KblkSin,
		      int KblkUin){


  KblkV = KblkVin;
  KblkS = KblkSin;
  KblkU = KblkUin;

  occa::printAvailableDevices();

  //device.setup("mode = OpenCL, platformID = 0, deviceID = 1");
  //device.setup("mode = OpenMP, platformID = 0, deviceID = 0");
  //device.setup("mode = Serial");
  device.setup("mode = CUDA, platformID = 0, deviceID = 0");
  //device.setCompiler("nvcc"); device.setCompilerFlags("--use_fast_math"); device.setCompilerFlags("--fmad=true");

  printf("KblkV = %d, KblkS = %d, KblkU = %d\n",KblkV,KblkS,KblkU);
  
  int K = mesh->K;
  int NpK = K*p_Np;
  int sz; // buffer size

  // nodal operators
  setOccaArray(mesh->Dr,c_Dr);
  setOccaArray(mesh->Ds,c_Ds);
  setOccaArray(mesh->Dt,c_Dt);
  setOccaArray(mesh->LIFT,c_LIFT);  
  setOccaIntArray(mesh->Fmask,c_Fmask);

  // nodal_WADG
  setOccaArray(mesh->Pq,c_Pq);
  setOccaArray(mesh->Cq,c_Cq);
  setOccaArray(mesh->Vq,c_Vq);

  // bern_WADG
  setOccaArray(mesh->PqB,c_PqB);
  setOccaArray(mesh->VqB,c_VqB);

  // projected c_BB coefficient of projected wave speed
  setOccaArray(mesh->CB,c_CB);
 
  // bern multiplication
  setOccaIntArray(mesh->col_id,c_col_id);
  setOccaArray(mesh->col_val,c_col_val);
  setOccaIntArray(mesh->L_id,c_L_id);

  // bern projection
  setOccaArray(mesh->ENMT_val,c_ENMT_val);
  setOccaArray(mesh->ENM_val, c_ENM_val);
  setOccaIntArray(mesh->ENMT_id,c_ENMT_id);
  setOccaIntArray(mesh->ENM_id,c_ENM_id);
  setOccaArray(mesh->co,c_co);
  setOccaArray(mesh->E,c_E);
  setOccaIntArray(mesh->ENMT_index, c_ENMT_index);

  // ==================== BERNSTEIN STUFF ==========================

  // bernstein Dmatrices (4 entries per row)
  sz = 4*p_Np*sizeof(int);
  c_Dvals4 = device.malloc(4*p_Np*sizeof(dfloat), mesh->D_vals[0]);

  // barycentric deriv indices organized for ILP
  // expand to a Np x 4 matrix for float4 storage
  int *D_ids1 = (int*) malloc(p_Np*4*sizeof(int));
  int *D_ids2 = (int*) malloc(p_Np*4*sizeof(int));
  int *D_ids3 = (int*) malloc(p_Np*4*sizeof(int));
  int *D_ids4 = (int*) malloc(p_Np*4*sizeof(int));
  MatrixXi D_ids(p_Np*4,4);
  for(int i = 0; i < p_Np; ++i){
    for (int j = 0; j < 4; ++j){
      D_ids(4*i+0,j) = mesh->D1_ids[i][j];
      D_ids(4*i+1,j) = mesh->D2_ids[i][j];
      D_ids(4*i+2,j) = mesh->D3_ids[i][j];
      D_ids(4*i+3,j) = mesh->D4_ids[i][j];
    }
      
    D_ids1[4*i + 0] = mesh->D1_ids[i][0];
    D_ids1[4*i + 1] = mesh->D2_ids[i][0];
    D_ids1[4*i + 2] = mesh->D3_ids[i][0];
    D_ids1[4*i + 3] = mesh->D4_ids[i][0];
    
    D_ids2[4*i + 0] = mesh->D1_ids[i][1];
    D_ids2[4*i + 1] = mesh->D2_ids[i][1];
    D_ids2[4*i + 2] = mesh->D3_ids[i][1];
    D_ids2[4*i + 3] = mesh->D4_ids[i][1];

    D_ids3[4*i + 0] = mesh->D1_ids[i][2];
    D_ids3[4*i + 1] = mesh->D2_ids[i][2];
    D_ids3[4*i + 2] = mesh->D3_ids[i][2];
    D_ids3[4*i + 3] = mesh->D4_ids[i][2];

    D_ids4[4*i + 0] = mesh->D1_ids[i][3];
    D_ids4[4*i + 1] = mesh->D2_ids[i][3];
    D_ids4[4*i + 2] = mesh->D3_ids[i][3];
    D_ids4[4*i + 3] = mesh->D4_ids[i][3];
  }
  
  setOccaIntArray(D_ids.col(0),c_D_ids1);
  setOccaIntArray(D_ids.col(1),c_D_ids2);
  setOccaIntArray(D_ids.col(2),c_D_ids3);
  setOccaIntArray(D_ids.col(3),c_D_ids4);  

  int L0_nnz = min(p_Nfp,7);
  mesh->L0_nnz = L0_nnz;
  setOccaArray(mesh->L0_vals,c_L0_vals);
  setOccaIntArray(mesh->L0_ids,c_L0_ids);  
  
#if (USE_SLICE_LIFT)  // should use for N > 5 (faster)
  // if using slice lift algo, use arrays to store degree reduction matrices 
  setOccaIntArray(mesh->EEL_id_vec,c_EEL_ids);
  setOccaArray(mesh->EEL_val_vec,c_EEL_vals);
#else
  setOccaArray(mesh->EEL_vals,c_EEL_vals);
  setOccaIntArray(mesh->EEL_ids,c_EEL_ids);  
#endif

  setOccaArray(mesh->cEL,c_cEL); // for slice-by-slice kernel

  int *h_slice_ids = (int*) malloc(p_Np*4*sizeof(int));
  for (int i = 0; i < p_Np; ++i){
    h_slice_ids[4*i+0] = mesh->slice_ids(i,0);
    h_slice_ids[4*i+1] = mesh->slice_ids(i,1);
    h_slice_ids[4*i+2] = mesh->slice_ids(i,2);
    h_slice_ids[4*i+3] = mesh->slice_ids(i,3);
  }
  c_slice_ids = device.malloc(p_Np*4*sizeof(int),h_slice_ids); // for float4 storage

  // =====================  geofacs ==================================

  double drdx, dsdx, dtdx;
  double drdy, dsdy, dtdy;
  double drdz, dsdz, dtdz, J;

  int sk = 0;
  int skP = -1;
  double *nxk = (double*) calloc(mesh->Nfaces,sizeof(double));
  double *nyk = (double*) calloc(mesh->Nfaces,sizeof(double));
  double *nzk = (double*) calloc(mesh->Nfaces,sizeof(double));
  double *sJk = (double*) calloc(mesh->Nfaces,sizeof(double));

  // [JC] packed geo + surface
  nvgeo = 9; // rst/xyz
  nfgeo = 4; // Fscale, (3)nxyz,
  ngeo = nfgeo*p_Nfaces + nvgeo; // nxyz + tau + Fscale (faces), G'*G (volume)
  dfloat *geo = (dfloat*) malloc(mesh->K*ngeo*sizeof(dfloat));
  dfloat *vgeo = (dfloat*) malloc(K*nvgeo*sizeof(dfloat));
  dfloat *fgeo = (dfloat*) malloc(K*nfgeo*p_Nfaces*sizeof(dfloat));

  dfloat FscaleMax = 0.f;
  for(int k=0;k<mesh->K;++k){

    GeometricFactors3d(mesh, k,
		       &drdx, &dsdx, &dtdx,
		       &drdy, &dsdy, &dtdy,
		       &drdz, &dsdz, &dtdz, &J);

    Normals3d(mesh, k, nxk, nyk, nzk, sJk);

    vgeo[k*nvgeo + 0] = drdx;    vgeo[k*nvgeo + 1] = drdy;    vgeo[k*nvgeo + 2] = drdz;
    vgeo[k*nvgeo + 3] = dsdx;    vgeo[k*nvgeo + 4] = dsdy;    vgeo[k*nvgeo + 5] = dsdz;
    vgeo[k*nvgeo + 6] = dtdx;    vgeo[k*nvgeo + 7] = dtdy;    vgeo[k*nvgeo + 8] = dtdz;

#if 0
    if (k==0){
      printf("rxyz = %f, %f, %f\n",drdx,drdy,drdz);
      printf("sxyz = %f, %f, %f\n",dsdx,dsdy,dsdz);
      printf("txyz = %f, %f, %f\n",dtdx,dtdy,dtdz);
      printf("J = %f\n\n",J);
    }
#endif

    for(int f=0;f<mesh->Nfaces;++f){

      dfloat Fscale = sJk[f]/J; //sJk[f]/(2.*J);
      dfloat nx = nxk[f];
      dfloat ny = nyk[f];
      dfloat nz = nzk[f];

      // for dt
      FscaleMax = max(FscaleMax,Fscale);

      fgeo[k*nfgeo*p_Nfaces + f*nfgeo + 0] = Fscale; // Fscale
      fgeo[k*nfgeo*p_Nfaces + f*nfgeo + 1] = nx;
      fgeo[k*nfgeo*p_Nfaces + f*nfgeo + 2] = ny;
      fgeo[k*nfgeo*p_Nfaces + f*nfgeo + 3] = nz;
    }
  }

  // correct vmapP for Nfields > 1
  int *h_vmapP = (int*) malloc(mesh->K*p_Nfp*p_Nfaces*sizeof(int));
  for (int e = 0; e < mesh->K; ++e){
    for (int i = 0; i < p_Nfp*p_Nfaces; ++i){
      int f = i/p_Nfp;
      int idP = mesh->vmapP[i + p_Nfp*p_Nfaces*e];
  
      int eNbr = mesh->EToE[e][f];
      idP -= p_Np*eNbr; // decrement
      idP += p_Np*p_Nfields*eNbr; // re-increment

      h_vmapP[i+p_Nfp*p_Nfaces*e] = idP;
    }
  }

  // storage for solution variables
  dfloat *f_Q    = (dfloat*) calloc(mesh->K*p_Nfields*p_Np, sizeof(dfloat));
  dfloat *f_resQ = (dfloat*) calloc(mesh->K*p_Nfields*p_Np, sizeof(dfloat));
  dfloat *f_rhsQ = (dfloat*) calloc(mesh->K*p_Nfields*p_Np, sizeof(dfloat));

  dfloat *f_P    = (dfloat*) calloc(mesh->K*p_Nfields*p_Np, sizeof(dfloat));
  dfloat *f_resP = (dfloat*) calloc(mesh->K*p_Nfields*p_Np, sizeof(dfloat));
  dfloat *f_rhsP = (dfloat*) calloc(mesh->K*p_Nfields*p_Np, sizeof(dfloat));

  // store the solution of BBWADG with M=1
  c_Q    = device.malloc(sizeof(dfloat)*mesh->K*p_Np*p_Nfields, f_Q);
  c_resQ = device.malloc(sizeof(dfloat)*mesh->K*p_Np*p_Nfields, f_resQ);
  c_rhsQ = device.malloc(sizeof(dfloat)*mesh->K*p_Np*p_Nfields, f_rhsQ);

  // store solution of full-quadrature WADG
  c_P    = device.malloc(sizeof(dfloat)*mesh->K*p_Np*p_Nfields, f_P);
  c_resP = device.malloc(sizeof(dfloat)*mesh->K*p_Np*p_Nfields, f_resP);
  c_rhsP = device.malloc(sizeof(dfloat)*mesh->K*p_Np*p_Nfields, f_rhsP);
  
  c_vgeo = device.malloc(mesh->K*nvgeo*sizeof(dfloat), vgeo);
  c_fgeo = device.malloc(mesh->K*nfgeo*p_Nfaces*sizeof(dfloat), fgeo);
  c_vmapP  = device.malloc(p_Nfp*p_Nfaces*mesh->K*sizeof(int),h_vmapP);

  // build kernels
  if (sizeof(dfloat)==8){
    dgInfo.addDefine("USE_DOUBLE", 1);
  }else{
    dgInfo.addDefine("USE_DOUBLE", 0);
  }
  dgInfo.addDefine("p_EEL_size",mesh->EEL_val_vec.rows());
  dgInfo.addDefine("p_EEL_nnz",mesh->EEL_nnz);
  dgInfo.addDefine("p_L0_nnz",min(p_Nfp,7)); // max 7 nnz with L0 matrix
  dgInfo.addDefine("p_Nfields",      p_Nfields); // wave equation
  dgInfo.addDefine("p_N",      p_N);
  dgInfo.addDefine("p_KblkV",  KblkV);
  dgInfo.addDefine("p_KblkS",  KblkS);
  dgInfo.addDefine("p_KblkU",  KblkU);
  dgInfo.addDefine("p_Np",      p_Np);
  dgInfo.addDefine("p_NMp",    p_NMp);
  dgInfo.addDefine("p_Nfp",     p_Nfp);
  dgInfo.addDefine("p_Nfaces",  p_Nfaces);
  dgInfo.addDefine("p_NfpNfaces",   p_Nfp*p_Nfaces);
  
  //add constant for projection_nodal
  dgInfo.addDefine("p_Vqrows",mesh->Vqrows);
  printf("Vqrows=%d\n", mesh->Vqrows);
  dgInfo.addDefine("p_NpNfields", p_Np*p_Nfields);

  // [JC] max threads
  int T = max(p_Np,p_Nfp*p_Nfaces);
  dgInfo.addDefine("p_T",T);
  dgInfo.addDefine("p_Nvgeo",nvgeo);
  dgInfo.addDefine("p_Nfgeo",nfgeo);

  std::string src = "okl/WaveKernels.okl";
  std::cout << "using src = " << src.c_str() << std::endl;

  printf("Building Bernstein kernels from %s\n",src.c_str());
  // bernstein kernels
  rk_volume_bern  = device.buildKernelFromSource(src.c_str(), "rk_volume_bern", dgInfo);

  printf("building rk_surface_bern from %s\n",src.c_str());
#if USE_SLICE_LIFT
  rk_surface_bern = device.buildKernelFromSource(src.c_str(), "rk_surface_bern_slice", dgInfo);
  printf("using slice-by-slice bern surface kernel; more efficient for N > 6\n");
#else
  printf("using non-optimal bern surface kernel; more efficient for N < 6\n");
  rk_surface_bern = device.buildKernelFromSource(src.c_str(), "rk_surface_bern", dgInfo);
#endif

  // nodal kernels
  rk_volume  = device.buildKernelFromSource(src.c_str(), "rk_volume", dgInfo);
  rk_surface = device.buildKernelFromSource(src.c_str(), "rk_surface", dgInfo);
  rk_update  = device.buildKernelFromSource(src.c_str(), "rk_update",dgInfo);
 

  // adaptive bern kernel
  rk_update_WADG  = device.buildKernelFromSource(src.c_str(), "rk_update_WADG", dgInfo);  
  rk_update_BB_WADG  = device.buildKernelFromSource(src.c_str(),"rk_update_BB_WADG",dgInfo);

 
  // estimate dt. may wish to replace with trace inequality constant
  dfloat dt = .25/((p_N+1)*(p_N+1)*FscaleMax);

  return (dfloat) dt;
}

// compute using quadrature 
void compute_error(Mesh *mesh, double time, dfloat *Q,
		   double(*uexptr)(double,double,double,double),
		   double &L2err, double &relL2err){

  // compute error
  L2err = 0.0;
  double L2norm = 0.0;
  int kChunk = max(mesh->K/10,1);
  for(int k=0;k<mesh->K;++k){

    //double J = mesh->J(0,k); // assuming J = constant (planar tet)
    for(int i = 0; i < mesh->Nq; ++i){

      double J = mesh->J(0,k);
      // interp to cubature nodes
      double x = 0.0; double y = 0.0; double z = 0.0; double uq = 0.0;
      for(int j=0;j<p_Np;++j){

	double Vq = mesh->Vq(i,j);
        x += Vq*mesh->x(j,k);
        y += Vq*mesh->y(j,k);
        z += Vq*mesh->z(j,k);
        uq += Vq*Q[j+p_Np*p_Nfields*k]; // get field value for pressure
      }

      double uex = (*uexptr)(x,y,z,(double)time);
      double err = uq-uex;

      L2err += err*err*mesh->wq(i)*J;
      L2norm += uex*uex*mesh->wq(i)*J;
    }
  }
  L2err = sqrt(L2err);
  relL2err = sqrt(L2err)/sqrt(L2norm);// something wrong here, sqrt already
  return;
}

// used for comparing the results of WADG and adaptive-BBDG
void compute_difference_Bern(Mesh *mesh, dfloat *Q, dfloat *P, double &L2error, double &reL2error){

  //compute error
  L2error = 0.0;
  double L2norm = 0.0;
  for(int k =0; k<mesh->K; ++k){
    for(int i=0; i<mesh->Nq;++i){

      double J = mesh->J(0,k);
      double uq = 0.0;
      double up = 0.0;

      for(int j=0; j<p_Np;++j){
	double Vq = mesh->Vq(i,j);
	uq += Vq * Q[j+p_Np*p_Nfields*k];
	up += Vq * P[j+p_Np*p_Nfields*k];
      }
      
      double err = uq - up;

      L2error += err*err*mesh->wq(i)*J;
      L2norm  += uq*uq*mesh->wq(i)*J;
    }
  }
  L2error = sqrt(L2error);
  reL2error=L2error/sqrt(L2norm);
  
  return;
}


// times planar kernels
void time_kernels(Mesh *mesh){

  double gflops = 0.0;
  double bw = 0.0;
  int nstep = 10;
  double denom = (double) (nstep * mesh->K * p_Np * p_Nfields); // nstep steps 
  FILE *timingFile = fopen ("blockTimings.txt","a");

  timeV = 0.0;
  timeS = 0.0;
  timeU = 0.0;
  
  occa::initTimer(device);
  
  // nodal kernels
  for (int step = 0; step < nstep; ++step){
    dfloat fdt = 1.f, rka = 1.f, rkb = 1.f;
    
    occa::tic("volume (bern)");
    rk_volume_bern(mesh->K, c_vgeo,
                   c_D_ids1, c_D_ids2, c_D_ids3, c_D_ids4, c_Dvals4,
                   c_Q, c_rhsQ);
    //rk_volume(mesh->K, c_vgeo, c_Dr, c_Ds, c_Dt, c_Q, c_rhsQ);
    device.finish();
    dfloat elapsedV = occa::toc("volume (bern)",rk_volume_bern, gflops, bw * sizeof(dfloat));
    
    
    occa::tic("surface (bern))");
    rk_surface_bern(mesh->K, c_fgeo, c_Fmask, c_vmapP,
                    c_slice_ids,c_EEL_ids, c_EEL_vals, c_L0_ids, c_L0_vals, c_cEL,
                    c_Q, c_rhsQ);
    //rk_surface(mesh->K, c_fgeo, c_Fmask, c_vmapP, c_LIFT, c_Q, c_rhsQ);
    device.finish();
    dfloat elapsedS = occa::toc("surface (bern)",rk_surface_bern, gflops, bw * sizeof(dfloat));

       
    occa::tic("update_WADG (FQWADG)");
    rk_update_WADG(mesh->K*p_Np*p_Nfields, rka, rkb, fdt, mesh->K, c_VqB, c_Cq, c_PqB, c_rhsQ, c_resQ, c_Q);
    device.finish();
    dfloat elapsedU = occa::toc("update (FQWADG)",rk_update_WADG, gflops, bw * sizeof(dfloat));
    
    timeV+=elapsedV;
    timeS+=elapsedS;
    timeU+=elapsedU;
  }
  occa::printTimer();
  printf("Bern kernels: elapsed time per timestep: V = %g, S = %g, U = %g\n", timeV/nstep,timeS/nstep,timeU/nstep);
  printf("FQWADG kernel time per element: %g\n", timeU/(nstep*mesh->K));
  timeV /= denom;
  timeS /= denom;
  timeU /= denom;
  printf("FQWADG kernels: elapsed time per dof per timestep: V = %g, S = %g, U = %g, Total = %g\n", timeV,timeS,timeU,timeV+timeS+timeU);
  //fprintf(timingFile,"%%Nodal kernels for N = %d\n KblkV(%d,%d) = %4.4g; KblkS(%d,%d) = %4.4g; KblkU(%d,%d) = %4.4g;\n",
  //        p_N,p_N,KblkV,timeV,p_N,KblkS,timeS,p_N,KblkU,timeU);
  
  
  // now time Bernstein kernels
  timeV = 0.0;
  timeS = 0.0;
  timeU = 0.0;

  occa::initTimer(device);
  // bern kernels
  for (int step = 0; step < nstep; ++step){
    dfloat fdt = 1.f, rka = 1.f, rkb = 1.f;

    occa::tic("volume (bern)");
    rk_volume_bern(mesh->K, c_vgeo,
                   c_D_ids1, c_D_ids2, c_D_ids3, c_D_ids4, c_Dvals4,
                   c_Q, c_rhsQ);
    device.finish();
    dfloat elapsedV = occa::toc("volume (bern)",rk_volume_bern, gflops, bw * sizeof(dfloat));
    
    occa::tic("surface (bern)");
    rk_surface_bern(mesh->K, c_fgeo, c_Fmask, c_vmapP,
                    c_slice_ids,c_EEL_ids, c_EEL_vals, c_L0_ids, c_L0_vals, c_cEL,
                    c_Q, c_rhsQ);
    device.finish();
    dfloat elapsedS = occa::toc("surface (bern)",rk_surface_bern, gflops, bw * sizeof(dfloat));
    
    
    occa::tic("update (BBWADG)");
    rk_update_BB_WADG(mesh->K, c_col_id,c_col_val,c_L_id,c_CB,c_ENMT_val,c_ENMT_id,c_ENM_val,c_ENM_id,c_E,c_co, c_ENMT_index,rka,rkb,fdt,c_rhsQ,c_resQ,c_Q);
    device.finish();
    dfloat elapsedU = occa::toc("update (BBWADG)", rk_update_BB_WADG, gflops, bw * sizeof(dfloat));
  
        
    timeV+=elapsedV;
    timeS+=elapsedS;
    timeU+=elapsedU;
  }
  occa::printTimer();

  printf("Bern kernels: elapsed time per timestep: V = %g, S = %g,  U = %g\n", timeV/nstep,timeS/nstep,timeU/nstep);
  printf("BBWADG kernel time per element: %g\n", timeU/(nstep*mesh->K));
  timeV /= denom;
  timeS /= denom;
  timeU /= denom;
  
  printf("BBWADG kernels: elapsed time per dof per timestep if m=0: V = %g, S = %g, U = %g, Total = %g\n", timeV,timeS,timeU,timeV+timeS+timeU);
  
#if USE_SLICE_LIFT
  fprintf(timingFile,"%%Bern kernels for N = %d\n KblkVB(%d,%d) = %4.4g; KblkSB_slice(%d,%d) = %4.4g;\n", p_N,p_N,KblkV,timeV,p_N,KblkS,timeS);
#else
  fprintf(timingFile,"%%Bern kernels for N = %d\n KblkVB(%d,%d) = %4.4g; KblkSB(%d,%d) = %4.4g;\n", p_N,p_N,KblkV,timeV,p_N,KblkS,timeS);
#endif
  fclose(timingFile);

}
// run RK
void Wave_RK_sample_error(Mesh *mesh, dfloat FinalTime, dfloat dt,
                          double(*uexptr)(double,double,double,double)){

  double time = 0;
  int    INTRK, tstep=0;

  int totalSteps = (int)floor(FinalTime/dt);
  int tchunk = max(totalSteps/10,1);

  int tsample = 2*(p_N+1)*(p_N+1); // sample at every (*) timesteps
  int num_samples = totalSteps/tsample + 1;

  double *L2err = (double*) calloc(num_samples,sizeof(double));
  double *tvec = (double*) calloc(num_samples,sizeof(double));
  int tstep_sample = 0;

  // for sampling L2 error in time
  FILE *L2errFile = fopen ("longTimeL2err.txt","w");
  dfloat *Q = (dfloat*) calloc(p_Nfields*mesh->K*p_Np, sizeof(dfloat));   // 4 fields
  dfloat *P = (dfloat*) calloc(p_Nfields*mesh->K*p_Np, sizeof(dfloat));
  /* outer time step loop  */
  while (time<FinalTime){
#if 1
    if (tstep%tsample==0){
      ++tstep_sample;
      double L2err, relL2err;
      WaveGetData3d(mesh, Q, P);
      compute_error(mesh, time, Q, uexptr, L2err, relL2err);
      fprintf(L2errFile,"t(%d) = %g; L2e(%d) = %g;\n",tstep_sample,time,tstep_sample,L2err);
    }
#endif

    if (tstep%tchunk==0){
      printf("on timestep %d/%d\n",tstep, totalSteps);
    }

    /* adjust final step to end exactly at FinalTime */
    if (time+dt > FinalTime) { dt = FinalTime-time; }

    for (INTRK=1; INTRK<=5; ++INTRK) {

      // compute DG rhs
      const dfloat fdt = dt;
      const dfloat fa = (float)mesh->rk4a[INTRK-1];
      const dfloat fb = (float)mesh->rk4b[INTRK-1];

      if (tstep==0 && INTRK==1){
        printf("running regular kernel\n\n");
      }
      RK_step(mesh, fa, fb, fdt);

    }

    time += dt;     /* increment current time */
    tstep++;        /* increment timestep */

  }

  fclose(L2errFile);
}

// run RK
void Wave_RK(Mesh *mesh, dfloat FinalTime, dfloat dt){

  double time = 0;
  int    INTRK, tstep = 0;

  int totalSteps = (int)floor(FinalTime/dt);
  int tchunk = max(totalSteps/10,1);

  int tsample = 2*(p_N+1)*(p_N+1); // sample at every (*) timesteps
  int num_samples = totalSteps/tsample + 1;

  double *L2err = (double*) calloc(num_samples,sizeof(double));
  double *tvec = (double*) calloc(num_samples,sizeof(double));
  int tstep_sample = 0;

  /* outer time step loop  */
  while (time<FinalTime){

    if (tstep%tchunk==0){
      printf("on timestep %d/%d\n",tstep, totalSteps);
    }

    /* adjust final step to end exactly at FinalTime */
    if (time+dt > FinalTime) { dt = FinalTime-time; }

    for (INTRK=1; INTRK<=5; ++INTRK) {

      // compute DG rhs
      const dfloat fdt = dt;
      const dfloat fa = (float)mesh->rk4a[INTRK-1];
      const dfloat fb = (float)mesh->rk4b[INTRK-1];

      RK_step(mesh, fa, fb, fdt);
    }

    time += dt;     /* increment current time */
    tstep++;        /* increment timestep */

  }
}

void RK_step(Mesh *mesh, dfloat rka, dfloat rkb, dfloat fdt){

  double gflops = 0.0;
  double bw = 0.0;
  int K = mesh->K;
  
#if USE_BERN
  rk_volume_bern(mesh->K, c_vgeo, c_D_ids1, c_D_ids2, c_D_ids3, c_D_ids4, c_Dvals4, c_Q, c_rhsQ);
  rk_surface_bern(mesh->K, c_fgeo, c_Fmask, c_vmapP, c_slice_ids, c_EEL_ids, c_EEL_vals, c_L0_ids, c_L0_vals, c_cEL, c_Q, c_rhsQ);
  rk_update_BB_WADG(mesh->K, c_col_id,c_col_val,c_L_id,c_CB,c_ENMT_val,c_ENMT_id,c_ENM_val,c_ENM_id,c_E,c_co, c_ENMT_index,rka,rkb,fdt,c_rhsQ,c_resQ,c_Q);
  
  int Ntotal = p_Nfields*p_Np*mesh->K;
  rk_volume_bern(mesh->K, c_vgeo, c_D_ids1, c_D_ids2, c_D_ids3, c_D_ids4, c_Dvals4, c_P, c_rhsP);
  rk_surface_bern(mesh->K, c_fgeo, c_Fmask, c_vmapP, c_slice_ids, c_EEL_ids, c_EEL_vals, c_L0_ids, c_L0_vals, c_cEL, c_P, c_rhsP);
  rk_update_WADG(Ntotal, rka, rkb, fdt, mesh->K, c_VqB, c_Cq, c_PqB, c_rhsP, c_resP, c_P);

  
#else
  
  int Ntotal = p_Nfields*p_Np*mesh->K;
  rk_volume(mesh->K, c_vgeo, c_Dr, c_Ds, c_Dt, c_Q, c_rhsQ);
  rk_surface(mesh->K, c_fgeo, c_Fmask, c_vmapP, c_LIFT, c_Q, c_rhsQ);
  rk_update(Ntotal, rka, rkb, fdt, c_rhsQ, c_resQ, c_Q);
  
#endif
 
#if 0
  dfloat *f_Q = (dfloat*) calloc(p_Nfields*mesh->K*p_Np, sizeof(dfloat));
  c_Q.copyTo(f_Q);
  for(int fld = 0; fld < p_Nfields; ++fld){
    printf("Field solution is %d: \n",fld);
    for(int i = 0; i < p_Np; ++i){
      for(int e = 0; e < mesh->K; ++e){
	printf("%f ",f_Q[i + fld*p_Np + e*p_Nfields*p_Np]);
      }
      printf("\n");
    }
    printf("\n\n");
  }
#endif
}


// set initial condition using interpolation
void WaveSetU0(Mesh *mesh, dfloat *Q, dfloat *P, dfloat time,
	       double(*uexptr)(double,double,double,double)){

  // write out field = fields 2-4 = 0 (velocity)
  for(int k = 0; k < mesh->K; ++k){
    // store local interpolant
    dfloat *Qloc = BuildVector(p_Np);
    dfloat *Ploc = BuildVector(p_Np);
    for(int i = 0; i < p_Np; ++i){
      double x = mesh->x(i,k);
      double y = mesh->y(i,k);
      double z = mesh->z(i,k);
      Qloc[i] = (*uexptr)(x,y,z,0.0);
      Ploc[i] = (*uexptr)(x,y,z,0.0);
    }

#if USE_BERN // convert nodal values to bernstein coefficients

    dfloat *Qtmp = BuildVector(p_Np);
    dfloat *Ptmp = BuildVector(p_Np);
      for (int i = 0; i < p_Np; ++i){
      Qtmp[i] = 0.f;
      Ptmp[i] = 0.f;
      for (int j = 0; j < p_Np; ++j){
	Qtmp[i] += mesh->invVB(i,j)*Qloc[j];
	Ptmp[i] += mesh->invVB(i,j)*Ploc[j];
      }
    }
    for (int i = 0; i < p_Np; ++i){
      Qloc[i] = Qtmp[i];
      Ploc[i] = Ptmp[i];
    }
        
#endif

    for (int i = 0; i < p_Np; ++i){
      int id = k*p_Np*p_Nfields + i;
      // set pressure by value of CavitySolution
      Q[id] = Qloc[i]; P[id] = Ploc[i];  id += p_Np;
      // set velocity to be all zeros
      Q[id] = 0.f;     P[id] = 0.f;      id += p_Np;
      Q[id] = 0.f;     P[id] = 0.f;      id += p_Np;
      Q[id] = 0.f;     P[id] = 0.f; 
    }
  }
}

// set initial condition using L2 projection
void WaveProjectU0(Mesh *mesh, dfloat *Q, dfloat time,
		   double(*uexptr)(double,double,double,double)){

  int Nq = mesh->Nq;
  VectorXd wq = mesh->wq;
  MatrixXd Vq = mesh->Vq;
  MatrixXd Qloc(p_Np,1);

  // write out field = fields 2-4 = 0 (velocity)
  for(int k = 0; k < mesh->K; ++k){

    // compute mass matrix explicitly w/quadrature
    double J = mesh->J(0,k); // constant over each element
    MatrixXd Mloc = Vq.transpose() * wq.asDiagonal() * J * Vq;

    // compute fxn at quad nodes
    MatrixXd uq(Nq,1);
    for (int i = 0; i < Nq; ++i){
      double xq = mesh->xq(i,k);
      double yq = mesh->yq(i,k);
      double zq = mesh->zq(i,k);
      double uqi = (*uexptr)(xq,yq,zq,0.0);
      uq(i,0) = uqi*wq(i)*J;
    }
    MatrixXd b = mesh->Vq.transpose() * uq;
    Qloc = mldivide(Mloc,b);

#if USE_BERN // convert nodal values to bernstein coefficients

    Qloc =  mesh->invVB*Qloc;

#endif

    // set pressure
    for (int i = 0; i < p_Np; ++i){
      int id = k*p_Np*p_Nfields + i;
      Q[id] = Qloc(i,0); id += p_Np;
      Q[id] = 0.f;       id += p_Np;
      Q[id] = 0.f;       id += p_Np;
      Q[id] = 0.f;

    }
  }
}


void WaveSetData3d(dfloat *Q, dfloat *P){
  c_Q.copyFrom(Q);
  c_P.copyFrom(P);
}


void WaveGetData3d(Mesh *mesh, dfloat *Q, dfloat *P){//dfloat *d_p, dfloat *d_fp, dfloat *d_fdpdn){

  c_Q.copyTo(Q);
  c_P.copyTo(P);

#if USE_BERN // convert back to nodal representation for L2 error, etc
  dfloat *Qtmp = BuildVector(p_Np);
  dfloat *Ptmp = BuildVector(p_Np);
  for (int fld = 0; fld < p_Nfields; fld++){
    for (int e = 0; e < mesh->K; ++e){
      for (int i = 0; i < p_Np; ++i){
	Qtmp[i] = 0.f;
	Ptmp[i] = 0.f;
	for (int j = 0; j < p_Np; ++j){
	  int id = j + fld*p_Np + e*p_Nfields*p_Np;
	  Qtmp[i] += mesh->VB(i,j)*Q[id];
	  Ptmp[i] += mesh->VB(i,j)*P[id];
	}
      }
      for (int i = 0; i < p_Np; ++i){
	int id = i + fld*p_Np + e*p_Nfields*p_Np;
	Q[id] = Qtmp[i];
	P[id] = Ptmp[i];
      }
    }
  }
#endif

}

void writeVisToGMSH(string fileName, Mesh *mesh, dfloat *Q, int iField, int Nfields){

  int timeStep = 0;
  double time = 0.0;
  int K = mesh->K;
  int Dim = 3;
  int N = p_N;


  MatrixXi monom(p_Np, Dim);
  MatrixXd vdm(p_Np, p_Np);
  for(int i=0, n=0; i<=N; i++){
    for(int j=0; j<=N; j++){
      for(int k=0; k<=N; k++){
	if(i+j+k <= N){
	  monom(n,0) = i;
	  monom(n,1) = j;
	  monom(n,2) = k;
	  n++;
	}
      }
    }
  }
  for(int m=0; m<p_Np; m++){
    for(int n=0; n<p_Np; n++){
      double r = mesh->r(n);
      double s = mesh->s(n);
      double t = mesh->t(n);
      vdm(m,n) = pow((r+1)/2.,monom(m,0)) *
	pow((s+1)/2.,monom(m,1)) * pow((t+1)/2.,monom(m,2));
    }
  }
  MatrixXd coeff = vdm.inverse();

  /// write the gmsh file
  ofstream *posFile;
  posFile = new ofstream(fileName.c_str());
  *posFile << "$MeshFormat" << endl;
  *posFile << "2.2 0 8" << endl;
  *posFile << "$EndMeshFormat" << endl;

  /// write the interpolation scheme
  *posFile << "$InterpolationScheme" << endl;
  *posFile << "\"MyInterpScheme\"" << endl;
  *posFile << "1" << endl;
  *posFile << "5 2" << endl;  // 5 2 = tets
  *posFile << p_Np << " " << p_Np << endl;  // size of matrix 'coeff'
  for(int m=0; m<p_Np; m++){
    for(int n=0; n<p_Np; n++)
      *posFile << coeff(m,n) << " ";
    *posFile << endl;
  }
  *posFile << p_Np << " " << Dim << endl;  // size of matrix 'monom'
  for(int n=0; n<p_Np; n++){
    for(int d=0; d<Dim; d++)
      *posFile << monom(n,d) << " ";
    *posFile << endl;
  }
  *posFile << "$EndInterpolationScheme" << endl;

  /// write element node data
  *posFile << "$ElementNodeData" << endl;
  *posFile << "2" << endl;
  *posFile << "\"" << "Field " << iField << "\"" << endl;  /// name of the view
  *posFile << "\"MyInterpScheme\"" << endl;
  *posFile << "1" << endl;
  *posFile << time << endl;
  *posFile << "3" << endl;
  *posFile << timeStep << endl;
  *posFile << "1" << endl;  /// ("numComp")
  *posFile << K << endl;  /// total number of elementNodeData in this file
  for(int k=0; k<K; k++){
    *posFile << mesh->EToGmshE(k) << " " << p_Np;
    for(int i=0; i<p_Np; i++)
      *posFile << " " << Q[i + iField*p_Np + k*p_Np*Nfields];
    *posFile << endl;
  }
  *posFile << "$EndElementNodeData" << endl;

  posFile->close();
  delete posFile;

}
