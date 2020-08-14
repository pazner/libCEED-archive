// Copyright (c) 2017, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-734707. All Rights
// reserved. See files LICENSE and NOTICE for details.
//
// This file is part of CEED, a collection of benchmarks, miniapps, software
// libraries and APIs for efficient high-order finite element and spectral
// element discretizations for exascale applications. For more information and
// source code availability see http://github.com/ceed.
//
// The CEED research is supported by the Exascale Computing Project 17-SC-20-SC,
// a collaborative effort of two U.S. Department of Energy organizations (Office
// of Science and the National Nuclear Security Administration) responsible for
// the planning and preparation of a capable exascale ecosystem, including
// software, applications, hardware, advanced system engineering and early
// testbed platforms, in support of the nation's exascale computing imperative.

/// @file
/// Geometric factors and mass operator for shallow-water example using PETSc

#ifndef setupgeo_h
#define setupgeo_h

#ifndef __CUDACC__
#  include <math.h>
#endif

// *****************************************************************************
// This QFunction sets up the geometric factors required for integration and
//   coordinate transformations. See ref: "A Discontinuous Galerkin Transport
//   Scheme on the Cubed Sphere", by Nair et al. (2004).
//
// Reference (parent) 2D coordinates: X \in [-1, 1]^2.
//
// Local 2D physical coordinates on the 2D manifold: x \in [-l, l]^2
//   with l half edge of the cube inscribed in the sphere. These coordinate
//   systems vary locally on each face (or panel) of the cube.
//
// (theta, lambda) represnt latitude and longitude coordinates.
//
// Change of coordinates from x (on the 2D manifold) to xx (phyisical 3D on
//   the sphere), i.e., "cube-to-sphere" A, with equidistant central projection:
//
//   For lateral panels (P0-P3):
//   A = R cos(theta)cos(lambda) / l * [cos(lambda)                       0]
//                                     [-sin(theta)sin(lambda)   cos(theta)]
//
//   For top panel (P4):
//   A = R sin(theta) / l * [cos(lambda)                        sin(lambda)]
//                          [-sin(theta)sin(lambda)   sin(theta)cos(lambda)]
//
//   For bottom panel(P5):
//   A = R sin(theta) / l * [-cos(lambda)                       sin(lambda)]
//                          [sin(theta)sin(lambda)    sin(theta)cos(lambda)]
//
// The inverse of A, A^{-1}, is the "sphere-to-cube" change of coordinates.
//
// The metric tensor g_{ij} = A^TA, and its inverse,
// g^{ij} = g_{ij}^{-1} = A^{-1}A^{-T}
//
// modJ represents the magnitude of the cross product of the columns of A, i.e.,
// J = det(g_{ij})
//
// The quadrature data is stored in the array qdata.
//
// We require the determinant of the Jacobian to properly compute integrals of
//   the form: int( u v ).
//
// *****************************************************************************
CEED_QFUNCTION(SetupGeo)(void *ctx, CeedInt Q,
                         const CeedScalar *const *in, CeedScalar *const *out) {
  // *INDENT-OFF*
  // Inputs
  const CeedScalar (*X)[CEED_Q_VLA] = (const CeedScalar(*)[CEED_Q_VLA])in[0],
                   (*J)[2][CEED_Q_VLA] = (const CeedScalar(*)[2][CEED_Q_VLA])in[1],
                   (*w) = in[2];
  // Outputs
  CeedScalar (*qdata)[CEED_Q_VLA] = (CeedScalar(*)[CEED_Q_VLA])out[0];
  // *INDENT-ON*

  CeedPragmaSIMD
  // Quadrature Point Loop to determine on which panel the element belongs to
  for (CeedInt i=0; i<Q; i++) {
    // Read local panel coordinates and relative panel index
    const CeedScalar x[2] = {X[0][i],
                             X[1][i]
                            };
    const CeedScalar pidx = X[2][i];
    if (pidx )
    break;
  }

  CeedPragmaSIMD
  // Quadrature Point Loop for metric factors
  for (CeedInt i=0; i<Q; i++) {
    // Read local panel coordinates and relative panel index
    const CeedScalar x[2] = {X[0][i],
                             X[1][i]
                            };
    const CeedScalar pidx = X[2][i];
    // Read dxxdX Jacobian entries, stored in columns
    // J_00 J_10
    // J_01 J_11
    // J_02 J_12
    const CeedScalar dxxdX[3][2] = {{J[0][0][i],
                                     J[1][0][i]},
                                    {J[0][1][i],
                                     J[1][1][i]},
                                    {J[0][2][i],
                                     J[1][2][i]}
                                   };
    // Setup
    // x = xx (xx^T xx)^{-1/2}
    // dx/dxx = I (xx^T xx)^{-1/2} - xx xx^T (xx^T xx)^{-3/2}
    const CeedScalar modxxsq = x[0]*x[0]+x[1]*x[1];
    CeedScalar xxsq[3][3];
    for (int j=0; j<3; j++)
      for (int k=0; k<3; k++)
        xxsq[j][k] = x[j]*x[k] / (sqrt(modxxsq) * modxxsq);

    const CeedScalar dxdxx[3][3] = {{1./sqrt(modxxsq) - xxsq[0][0],
                                     -xxsq[0][1],
                                     -xxsq[0][2]},
                                    {-xxsq[1][0],
                                     1./sqrt(modxxsq) - xxsq[1][1],
                                     -xxsq[1][2]},
                                    {-xxsq[2][0],
                                     -xxsq[2][1],
                                     1./sqrt(modxxsq) - xxsq[2][2]}
                                   };

    CeedScalar dxdX[3][2];
    for (CeedInt j=0; j<3; j++) {
      for (CeedInt k=0; k<2; k++) {
        dxdX[j][k] = 0;
        for (CeedInt l=0; l<3; l++)
          dxdX[j][k] += dxdxx[j][l]*dxxdX[l][k];
      }
    }
    // J is given by the cross product of the columns of dxdX
    const CeedScalar J[3] = {dxdX[1][0]*dxdX[2][1] - dxdX[2][0]*dxdX[1][1],
                             dxdX[2][0]*dxdX[0][1] - dxdX[0][0]*dxdX[2][1],
                             dxdX[0][0]*dxdX[1][1] - dxdX[1][0]*dxdX[0][1]
                            };

    // Use the magnitude of J as our detJ (volume scaling factor)
    const CeedScalar modJ = sqrt(J[0]*J[0]+J[1]*J[1]+J[2]*J[2]);

    // Interp-to-Interp qdata
    qdata[0][i] = modJ * w[i];

    // dxdX_k,j * dxdX_j,k
    CeedScalar dxdXTdxdX[2][2];
    for (CeedInt j=0; j<2; j++) {
      for (CeedInt k=0; k<2; k++) {
        dxdXTdxdX[j][k] = 0;
        for (CeedInt l=0; l<3; l++)
          dxdXTdxdX[j][k] += dxdX[l][j]*dxdX[l][k];
      }
    }
    const CeedScalar detdxdXTdxdX =  dxdXTdxdX[0][0] * dxdXTdxdX[1][1]
                                    -dxdXTdxdX[1][0] * dxdXTdxdX[0][1];

    // Compute inverse of dxdXTdxdX, needed for the pseudoinverse. This is also
    // equivalent to the 2x2 metric tensor g^{ij}, needed for the
    // Grad-to-Grad qdata (pseudodXdx * pseudodXdxT, which simplifies to
    // dxdXTdxdXinv)
    CeedScalar dxdXTdxdXinv[2][2];
    dxdXTdxdXinv[0][0] =  dxdXTdxdX[1][1] / detdxdXTdxdX;
    dxdXTdxdXinv[0][1] = -dxdXTdxdX[0][1] / detdxdXTdxdX;
    dxdXTdxdXinv[1][0] = -dxdXTdxdX[1][0] / detdxdXTdxdX;
    dxdXTdxdXinv[1][1] =  dxdXTdxdX[0][0] / detdxdXTdxdX;

    // Compute the pseudo inverse of dxdX
    CeedScalar pseudodXdx[2][3];
    for (CeedInt j=0; j<2; j++) {
      for (CeedInt k=0; k<3; k++) {
        pseudodXdx[j][k] = 0;
        for (CeedInt l=0; l<2; l++)
          pseudodXdx[j][k] += dxdXTdxdXinv[j][l]*dxdX[k][l];
      }
    }

    // Interp-to-Grad qdata
    // Pseudo inverse of dxdX: (x_i,j)+ = X_i,j
    qdata[1][i] = pseudodXdx[0][0];
    qdata[2][i] = pseudodXdx[0][1];
    qdata[3][i] = pseudodXdx[0][2];
    qdata[4][i] = pseudodXdx[1][0];
    qdata[5][i] = pseudodXdx[1][1];
    qdata[6][i] = pseudodXdx[1][2];

    // Grad-to-Grad qdata stored in Voigt convention
    qdata[7][i] = dxdXTdxdXinv[0][0];
    qdata[8][i] = dxdXTdxdXinv[1][1];
    qdata[9][i] = dxdXTdxdXinv[0][1];

    // Terrain topography, hs
    qdata[10][i] = sin(x[0]) + cos(x[1]); // put 0 for constant flat topography
  } // End of Quadrature Point Loop

  // Return
  return 0;
}

#endif // setupgeo_h
