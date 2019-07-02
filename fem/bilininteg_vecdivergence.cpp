// Copyright (c) 2019, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#include "../general/forall.hpp"
#include "bilininteg.hpp"
#include "gridfunc.hpp"

using namespace std;

namespace mfem
{

// PA Vector Divergence Integrator

#ifdef MFEM_USE_OCCA
// OCCA 2D Assemble kernel
static void OccaPAVectorDivergenceSetup2D(const int D1D,
                                          const int Q1D,
                                          const int NE,
                                          const Array<double> &W,
                                          const Vector &J,
                                          const double COEFF,
                                          Vector &op)
{
   // TODO: Take from diffusion?
}

// OCCA 3D Assemble kernel
static void OccaPAVectorDivergenceSetup3D(const int D1D,
                                          const int Q1D,
                                          const int NE,
                                          const Array<double> &W,
                                          const Vector &J,
                                          const double COEFF,
                                          Vector &op)
{
   // TODO: Take from diffusion?
}
#endif // MFEM_USE_OCCA

// PA Vector Divergence Assemble 2D kernel
static void PAVectorDivergenceSetup2D(const int Q1D,
                                      const int NE,
                                      const Array<double> &w,
                                      const Vector &j,
                                      const double COEFF,
                                      Vector &op)
{
   const int NQ = Q1D*Q1D;
   auto W = w.Read();

   auto J = Reshape(j.Read(), NQ, 2, 2, NE);
   auto y = Reshape(op.Write(), NQ, 4, NE);

   MFEM_FORALL(e, NE,
   {
      for (int q = 0; q < NQ; ++q)
      {
         const double J11 = J(q,0,0,e);
         const double J21 = J(q,1,0,e);
         const double J12 = J(q,0,1,e);
         const double J22 = J(q,1,1,e);
         // Store wq * Q * adj(J)
         y(q,0,e) = W[q] * COEFF *  J22; // 1,1
         y(q,1,e) = W[q] * COEFF * -J12; // 1,2
         y(q,2,e) = W[q] * COEFF * -J21; // 2,1
         y(q,3,e) = W[q] * COEFF *  J11; // 2,2
      }
   });
}

// PA Vector Divergence Assemble 3D kernel
static void PAVectorDivergenceSetup3D(const int Q1D,
                                      const int NE,
                                      const Array<double> &w,
                                      const Vector &j,
                                      const double COEFF,
                                      Vector &op)
{
   const int NQ = Q1D*Q1D*Q1D;
   auto W = w.Read();
   auto J = Reshape(j.Read(), NQ, 3, 3, NE);
   auto y = Reshape(op.Write(), NQ, 9, NE);
   MFEM_FORALL(e, NE,
   {
      for (int q = 0; q < NQ; ++q)
      {
         const double J11 = J(q,0,0,e);
         const double J21 = J(q,1,0,e);
         const double J31 = J(q,2,0,e);
         const double J12 = J(q,0,1,e);
         const double J22 = J(q,1,1,e);
         const double J32 = J(q,2,1,e);
         const double J13 = J(q,0,2,e);
         const double J23 = J(q,1,2,e);
         const double J33 = J(q,2,2,e);
         const double cw  = W[q] * COEFF;
         // adj(J)
         const double A11 = (J22 * J33) - (J23 * J32);
         const double A12 = (J32 * J13) - (J12 * J33);
         const double A13 = (J12 * J23) - (J22 * J13);
         const double A21 = (J31 * J23) - (J21 * J33);
         const double A22 = (J11 * J33) - (J13 * J31);
         const double A23 = (J21 * J13) - (J11 * J23);
         const double A31 = (J21 * J32) - (J31 * J22);
         const double A32 = (J31 * J12) - (J11 * J32);
         const double A33 = (J11 * J22) - (J12 * J21);
         // Store wq * Q * adj(J)
         y(q,0,e) = cw * A11; // 1,1
         y(q,1,e) = cw * A12; // 1,2
         y(q,2,e) = cw * A13; // 1,3
         y(q,3,e) = cw * A21; // 2,1
         y(q,4,e) = cw * A22; // 2,2
         y(q,5,e) = cw * A23; // 2,3
         y(q,6,e) = cw * A31; // 3,1
         y(q,7,e) = cw * A32; // 3,2
         y(q,8,e) = cw * A33; // 3,3
      }
   });
}

static void PAVectorDivergenceSetup(const int dim,
                                    const int D1D,
                                    const int Q1D,
                                    const int NE,
                                    const Array<double> &W,
                                    const Vector &J,
                                    const double COEFF,
                                    Vector &op)
{
   if (dim == 1) { MFEM_ABORT("dim==1 not supported in PAVectorDivergenceSetup"); }
   if (dim == 2)
   {
#ifdef MFEM_USE_OCCA
      if (DeviceCanUseOcca())
      {
         OccaPAVectorDivergenceSetup2D(D1D, Q1D, NE, W, J, COEFF, op);
         return;
      }
#endif // MFEM_USE_OCCA
      PAVectorDivergenceSetup2D(Q1D, NE, W, J, COEFF, op);
   }
   if (dim == 3)
   {
#ifdef MFEM_USE_OCCA
      if (DeviceCanUseOcca())
      {
         OccaPAVectorDivergenceSetup3D(D1D, Q1D, NE, W, J, COEFF, op);
         return;
      }
#endif // MFEM_USE_OCCA
      PAVectorDivergenceSetup3D(Q1D, NE, W, J, COEFF, op);
   }
}

void VectorDivergenceIntegrator::AssemblePA(const FiniteElementSpace &trial_fes,
                                            const FiniteElementSpace &test_fes)
{
   // Assumes tensor-product elements ordered by nodes
   MFEM_ASSERT(trial_fes.GetOrdering() == Ordering::byNODES,
               "PA Only supports Ordering::byNODES!");
   Mesh *mesh = trial_fes.GetMesh();
   const FiniteElement &trial_fe = *trial_fes.GetFE(0);
   const FiniteElement &test_fe = *test_fes.GetFE(0);
   ElementTransformation *trans = mesh->GetElementTransformation(0);
   const IntegrationRule *ir = IntRule ? IntRule : &GetRule(trial_fe, test_fe, *trans);
   const int dims = trial_fe.GetDim();
   const int dimsToStore = dims * dims;
   const int nq = ir->GetNPoints();
   dim = mesh->Dimension();
   ne = trial_fes.GetNE();
   geom = mesh->GetGeometricFactors(*ir, GeometricFactors::JACOBIANS);
   trial_maps = &trial_fe.GetDofToQuad(*ir, DofToQuad::TENSOR);
   trial_dofs1D = trial_maps->ndof;
   trial_quad1D = trial_maps->nqpt;
   test_maps  = &test_fe.GetDofToQuad(*ir, DofToQuad::TENSOR);
   test_dofs1D = test_maps->ndof;
   test_quad1D = test_maps->nqpt; // TODO: Is this necessary?
   pa_data.SetSize(dimsToStore * nq * ne, Device::GetMemoryType());
   ConstantCoefficient *cQ = dynamic_cast<ConstantCoefficient*>(Q);
   MFEM_VERIFY(cQ != NULL, "only ConstantCoefficient is supported!");
   const double coeff = cQ->constant;
   PAVectorDivergenceSetup(dim, trial_dofs1D, trial_quad1D, ne, ir->GetWeights(), 
                           geom->J, coeff, pa_data);
}

#ifdef MFEM_USE_OCCA
// OCCA PA VectorDivergence Apply 2D kernel
static void OccaPAVectorDivergenceApply2D(const int D1D,
                                          const int Q1D,
                                          const int NE,
                                          const Array<double> &B,
                                          const Array<double> &G,
                                          const Array<double> &Bt,
                                          const Array<double> &Gt,
                                          const Vector &op,
                                          const Vector &x,
                                          Vector &y)
{
   MFEM_ABORT("VectorDivergence OCCA NOT YET SUPPORTED!"); // TODO
}

// OCCA PA VectorDivergence Apply 3D kernel
static void OccaPAVectorDivergenceApply3D(const int D1D,
                                          const int Q1D,
                                          const int NE,
                                          const Array<double> &B,
                                          const Array<double> &G,
                                          const Array<double> &Bt,
                                          const Array<double> &Gt,
                                          const Vector &op,
                                          const Vector &x,
                                          Vector &y)
{
   MFEM_ABORT("VectorDivergence OCCA NOT YET SUPPORTED!"); // TODO
}
#endif

// PA VectorDivergence Apply 2D kernel
template<int T_D1D = 0, int T_Q1D = 0> static
void PAVectorDivergenceApply2D(const int NE,
                               const Array<double> &b,
                               const Array<double> &g,
                               const Array<double> &bt,
                               const Array<double> &gt,
                               const Vector &_op,
                               const Vector &_x,
                               Vector &_y,
                               const int d1d = 0,
                               const int q1d = 0)
{
   const int D1D = T_D1D ? T_D1D : d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   MFEM_VERIFY(D1D <= MAX_D1D, "");
   MFEM_VERIFY(Q1D <= MAX_Q1D, "");
   auto B = Reshape(b.Read(), Q1D, D1D);
   auto G = Reshape(g.Read(), Q1D, D1D);
   auto Bt = Reshape(bt.Read(), D1D, Q1D);
   auto Gt = Reshape(gt.Read(), D1D, Q1D);
   auto op = Reshape(_op.Read(), Q1D*Q1D, 3, NE);
   auto x = Reshape(_x.Read(), D1D, D1D, NE, 2);
   auto y = Reshape(_y.ReadWrite(), D1D, D1D, NE);
   MFEM_FORALL(e, NE,
   {
      const int D1D = T_D1D ? T_D1D : d1d;
      const int Q1D = T_Q1D ? T_Q1D : q1d;
      // the following variables are evaluated at compile time
      constexpr int max_D1D = T_D1D ? T_D1D : MAX_D1D;
      constexpr int max_Q1D = T_Q1D ? T_Q1D : MAX_Q1D;

      double grad[max_Q1D][max_Q1D][2];
      for (int qy = 0; qy < Q1D; ++qy)
      {
         for (int qx = 0; qx < Q1D; ++qx)
         {
            grad[qy][qx][0] = 0.0;
            grad[qy][qx][1] = 0.0;
         }
      }
      for (int dy = 0; dy < D1D; ++dy)
      {
         double gradX[max_Q1D][2];
         for (int qx = 0; qx < Q1D; ++qx)
         {
            gradX[qx][0] = 0.0;
            gradX[qx][1] = 0.0;
         }
         for (int dx = 0; dx < D1D; ++dx)
         {
            const double s = x(dx,dy,e);
            for (int qx = 0; qx < Q1D; ++qx)
            {
               gradX[qx][0] += s * B(qx,dx);
               gradX[qx][1] += s * G(qx,dx);
            }
         }
         for (int qy = 0; qy < Q1D; ++qy)
         {
            const double wy  = B(qy,dy);
            const double wDy = G(qy,dy);
            for (int qx = 0; qx < Q1D; ++qx)
            {
               grad[qy][qx][0] += gradX[qx][1] * wy;
               grad[qy][qx][1] += gradX[qx][0] * wDy;
            }
         }
      }
      // Calculate Dxy, xDy in plane
      for (int qy = 0; qy < Q1D; ++qy)
      {
         for (int qx = 0; qx < Q1D; ++qx)
         {
            const int q = qx + qy * Q1D;

            const double O11 = op(q,0,e);
            const double O12 = op(q,1,e);
            const double O22 = op(q,2,e);

            const double gradX = grad[qy][qx][0];
            const double gradY = grad[qy][qx][1];

            grad[qy][qx][0] = (O11 * gradX) + (O12 * gradY);
            grad[qy][qx][1] = (O12 * gradX) + (O22 * gradY);
         }
      }
      for (int qy = 0; qy < Q1D; ++qy)
      {
         double gradX[max_D1D][2];
         for (int dx = 0; dx < D1D; ++dx)
         {
            gradX[dx][0] = 0;
            gradX[dx][1] = 0;
         }
         for (int qx = 0; qx < Q1D; ++qx)
         {
            const double gX = grad[qy][qx][0];
            const double gY = grad[qy][qx][1];
            for (int dx = 0; dx < D1D; ++dx)
            {
               const double wx  = Bt(dx,qx);
               const double wDx = Gt(dx,qx);
               gradX[dx][0] += gX * wDx;
               gradX[dx][1] += gY * wx;
            }
         }
         for (int dy = 0; dy < D1D; ++dy)
         {
            const double wy  = Bt(dy,qy);
            const double wDy = Gt(dy,qy);
            for (int dx = 0; dx < D1D; ++dx)
            {
               y(dx,dy,e) += ((gradX[dx][0] * wy) + (gradX[dx][1] * wDy));
            }
         }
      }
   });
}


static void PAVectorDivergenceApply(const int dim,
                                    const int D1D,
                                    const int Q1D,
                                    const int pD1D,
                                    const int pQ1D,
                                    const int NE,
                                    const Array<double> &B,
                                    const Array<double> &G,
                                    const Array<double> &Bt,
                                    const Array<double> &Gt,
                                    const Vector &op,
                                    const Vector &x,
                                    Vector &y)
{
#ifdef MFEM_USE_OCCA
   if (DeviceCanUseOcca())
   {
      if (dim == 2)
      {
         OccaPAVectorDivergenceApply2D(D1D, Q1D, NE, B, G, Bt, Gt, op, x, y);
         return;
      }
      if (dim == 3)
      {
         OccaPAVectorDivergenceApply3D(D1D, Q1D, NE, B, G, Bt, Gt, op, x, y);
         return;
      }
      MFEM_ABORT("OCCA PAVectorDivergenceApply unknown kernel!");
   }
#endif // MFEM_USE_OCCA

   if (Device::Allows(Backend::RAJA_CUDA))
   {
      if (dim == 2)
      {
         switch ((D1D << 4 ) | Q1D)
         {
            case 0x22: return PAVectorDivergenceApply2D<2,2>(NE,B,G,Bt,Gt,op,x,y);
            case 0x33: return PAVectorDivergenceApply2D<3,3>(NE,B,G,Bt,Gt,op,x,y);
            case 0x44: return PAVectorDivergenceApply2D<4,4>(NE,B,G,Bt,Gt,op,x,y);
            case 0x55: return PAVectorDivergenceApply2D<5,5>(NE,B,G,Bt,Gt,op,x,y);
            case 0x66: return PAVectorDivergenceApply2D<6,6>(NE,B,G,Bt,Gt,op,x,y);
            case 0x77: return PAVectorDivergenceApply2D<7,7>(NE,B,G,Bt,Gt,op,x,y);
            case 0x88: return PAVectorDivergenceApply2D<8,8>(NE,B,G,Bt,Gt,op,x,y);
            case 0x99: return PAVectorDivergenceApply2D<9,9>(NE,B,G,Bt,Gt,op,x,y);
            default:   return PAVectorDivergenceApply2D(NE,B,G,Bt,Gt,op,x,y,D1D,Q1D);
         }
      }
      if (dim == 3)
      {
         switch ((D1D << 4 ) | Q1D)
         {
            case 0x23: return PAVectorDivergenceApply3D<2,3>(NE,B,G,Bt,Gt,op,x,y);
            case 0x34: return PAVectorDivergenceApply3D<3,4>(NE,B,G,Bt,Gt,op,x,y);
            case 0x45: return PAVectorDivergenceApply3D<4,5>(NE,B,G,Bt,Gt,op,x,y);
            case 0x56: return PAVectorDivergenceApply3D<5,6>(NE,B,G,Bt,Gt,op,x,y);
            case 0x67: return PAVectorDivergenceApply3D<6,7>(NE,B,G,Bt,Gt,op,x,y);
            case 0x78: return PAVectorDivergenceApply3D<7,8>(NE,B,G,Bt,Gt,op,x,y);
            case 0x89: return PAVectorDivergenceApply3D<8,9>(NE,B,G,Bt,Gt,op,x,y);
            default:   return PAVectorDivergenceApply3D(NE,B,G,Bt,Gt,op,x,y,D1D,Q1D);
         }
      }
   }
   else if (dim == 2)
   {
      switch ((D1D << 4 ) | Q1D)
      {
         case 0x22: return SmemPAVectorDivergenceApply2D<2,2,16>(NE,B,G,Bt,Gt,op,x,y);
         case 0x33: return SmemPAVectorDivergenceApply2D<3,3,16>(NE,B,G,Bt,Gt,op,x,y);
         case 0x44: return SmemPAVectorDivergenceApply2D<4,4,8>(NE,B,G,Bt,Gt,op,x,y);
         case 0x55: return SmemPAVectorDivergenceApply2D<5,5,8>(NE,B,G,Bt,Gt,op,x,y);
         case 0x66: return SmemPAVectorDivergenceApply2D<6,6,4>(NE,B,G,Bt,Gt,op,x,y);
         case 0x77: return SmemPAVectorDivergenceApply2D<7,7,4>(NE,B,G,Bt,Gt,op,x,y);
         case 0x88: return SmemPAVectorDivergenceApply2D<8,8,2>(NE,B,G,Bt,Gt,op,x,y);
         case 0x99: return SmemPAVectorDivergenceApply2D<9,9,2>(NE,B,G,Bt,Gt,op,x,y);
         default:   return PAVectorDivergenceApply2D(NE,B,G,Bt,Gt,op,x,y,D1D,Q1D);
      }
   }
   else if (dim == 3)
   {
      switch ((D1D << 4 ) | Q1D)
      {
         case 0x23: return SmemPAVectorDivergenceApply3D<2,3>(NE,B,G,Bt,Gt,op,x,y);
         case 0x34: return SmemPAVectorDivergenceApply3D<3,4>(NE,B,G,Bt,Gt,op,x,y);
         case 0x45: return SmemPAVectorDivergenceApply3D<4,5>(NE,B,G,Bt,Gt,op,x,y);
         case 0x56: return SmemPAVectorDivergenceApply3D<5,6>(NE,B,G,Bt,Gt,op,x,y);
         case 0x67: return SmemPAVectorDivergenceApply3D<6,7>(NE,B,G,Bt,Gt,op,x,y);
         case 0x78: return SmemPAVectorDivergenceApply3D<7,8>(NE,B,G,Bt,Gt,op,x,y);
         case 0x89: return SmemPAVectorDivergenceApply3D<8,9>(NE,B,G,Bt,Gt,op,x,y);
         default:   return PAVectorDivergenceApply3D(NE,B,G,Bt,Gt,op,x,y,D1D,Q1D);
      }
   }
   MFEM_ABORT("Unknown kernel.");
}

// PA VectorDivergence Apply kernel
void VectorDivergenceIntegrator::AddMultPA(const Vector &x, Vector &y) const
{
   // TODO: confirm this choice of maps is correct
   PAVectorDivergenceApply(dim, trial_dofs1D, trial_quad1D, test_dofs1D, test_quad1D, ne,
                           trial_maps->B, trial_maps->G, test_maps->B, test_maps->G, 
                           pa_data, x, y);
}

} // namespace mfem