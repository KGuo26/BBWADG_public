#ifndef _MESH_HEADER
#define _MESH_HEADER

#include <occa.hpp>

#include "Basis.h"

// default order
#ifndef p_N
#define p_N 6
#endif

#define NODETOL   1e-6
//#define NODETOL   1e-5
#define p_Nfp     ((p_N+1)*(p_N+2)/2)
#define p_Np      ((p_N+1)*(p_N+2)*(p_N+3)/6)
#define p_Nfields 4 // wave equation
#define p_Nfaces  4
#define p_NMp     ((p_N+2)*(p_N+3)*(p_N+4)/6)

//#define PFAC 8
//#define BSIZE   (PFAC*((p_Np+PFAC-1)/PFAC))
//#define BSIZE p_Np

#define max(a,b)  ( (a>b)?a:b )
#define min(a,b)  ( (a<b)?a:b )

/* list of vertices on each edge */

typedef struct foo {

  // =============== mesh stuff ===================

  int Nverts; /* number of vertices per element */
  int Nfaces; /* number of faces per element */
  int Nedges; /* number of edges per element (3d only) */

  int Nv;     /* number of mesh vertices */
  int K;      /* number of mesh elements */
  int **EToV; /* element to global vertex list  */
  int **EToE; /* element to neighbor element (elements numbered by their proc) */
  int **EToF; /* element to neighbor face    (element local number 0,1,2) */

  VectorXi EToGmshE;

  int *bcflag; /* vector. entry n is 1 if vertex n is on a boundary */

  //dfloat **GX; /* x-coordinates of element vertices */
  //dfloat **GY; /* y-coordinates of element vertices */
  //dfloat **GZ; /* z-coordinates of element vertices (3d) */
  MatrixXd GX,GY,GZ;

  // =============== reference elem stuff ===================

  /* high order node info */
  //MatrixXi Fmask;
  int   **FmaskC; /* face node numbers in element volume data */
  MatrixXi Fmask;
  MatrixXi faceVolPerm  ; // permutation of f=0 lift matrix rows for f = 1,2,3
  VectorXd r,s,t;

  int Nq;
  VectorXd wq, rq, sq, tq;
  MatrixXd xq,yq,zq;
  MatrixXd rxq,ryq,rzq,sxq,syq,szq,txq,tyq,tzq,Jq;
  MatrixXd Vq, Vrq, Vsq, Vtq;

  int Nfq;
  VectorXd wfqFace, rfq,sfq,tfq,wfq;
  MatrixXd nxq,nyq,nzq,sJq;

  MatrixXd V, Dr, Ds, Dt, LIFT; // Eigen-based matrices: cubature, nodal deriv/lift
  MatrixXd VfqFace, Vfq;
  MatrixXd VB, invVB; // Bernstein conversion

  // sparse bernstein arrays
  MatrixXi D_ids1,D_ids2,D_ids3,D_ids4;
  int **D1_ids, **D2_ids, **D3_ids, **D4_ids;
  dfloat **D_vals;

  // decomposition of BB lift operator
  MatrixXd EEL_vals;
  MatrixXi EEL_ids;
  int EEL_nnz, L0_nnz; // max number of nonzeros in EEL scaled degree elevation matrix

  MatrixXd L0_vals;
  MatrixXi L0_ids;
  VectorXd cEL;

  // BB lift operator helper arrays
  MatrixXi vol_ids, slice_ids;
  VectorXd EEL_val_vec;
  VectorXi EEL_id_vec;

  // Projection matrices w.r.t nodal basis
  MatrixXd Pq,Cq;
  int Vqrows;

  // projection matrices used for WADG w.r.t BB basis
  MatrixXd PqB, VqB;
  
  // Lval and Lid that used for BB multiplication
  MatrixXd col_val;
  MatrixXi col_id;
  MatrixXi L_id;
  
  // Projection matrices used for BB basis
  MatrixXd ENMT_val, ENM_val;
  MatrixXd  co, E;
  MatrixXi ENMT_id, ENM_id, ENMT_index;
  
  // =============== global DG stuff ===================

  MatrixXd x,y,z; // xyz-coordinates of element nodes (3d)
  MatrixXd rx,ry,rz,sx,sy,sz,tx,ty,tz,J; // geofacs
  MatrixXd nx,ny,nz,sJ; // surface geofacs


  //Vector that stores the best approximation degree of c^2 function w.r.t BB basis
  MatrixXi  m0,m1,m2;
  int k0,k1,k2;
  //the projected coefficients of c^2 on each element
  MatrixXd CB;

  
  // DG connectivity maps
  int *mapM,  *mapP; // face node id of +/- nodes (used in mesh connectivity)
  int *vmapM, *vmapP; // volume id of -/+ ve trace of face node

  // time stepping constants
  dfloat *rk4a, *rk4b, *rk4c;

  double hMax; // mesh size - computed
}Mesh;


#endif
