#include "navier_solver.hpp"
#include "../../general/forall.hpp"
#include <fstream>

using namespace mfem;
using namespace navier;

void CopyDBFIntegrators(ParBilinearForm *src, ParBilinearForm *dst)
{
   Array<BilinearFormIntegrator *> *bffis = src->GetDBFI();
   for (int i = 0; i < bffis->Size(); ++i)
   {
      dst->AddDomainIntegrator((*bffis)[i]);
   }
}

NavierSolver::NavierSolver(ParMesh *mesh, int order, double kin_vis)
   : pmesh(mesh), order(order), kin_vis(kin_vis),
     rules_ni(0, Quadrature1D::GaussLobatto)
{
   vfec = new H1_FECollection(order, pmesh->Dimension());
   pfec = new H1_FECollection(order);
   vfes = new ParFiniteElementSpace(pmesh, vfec, pmesh->Dimension());
   pfes = new ParFiniteElementSpace(pmesh, pfec);

   // Check if fully periodic mesh
   if (pmesh->bdr_attributes.Size())
   {
      vel_ess_attr.SetSize(pmesh->bdr_attributes.Max());
      vel_ess_attr = 0;

      pres_ess_attr.SetSize(pmesh->bdr_attributes.Max());
      pres_ess_attr = 0;
   }

   int vfes_truevsize = vfes->GetTrueVSize();
   int pfes_truevsize = pfes->GetTrueVSize();

   un.SetSize(vfes_truevsize);
   un = 0.0;
   un_next.SetSize(vfes_truevsize);
   un_next = 0.0;
   unm1.SetSize(vfes_truevsize);
   unm1 = 0.0;
   unm2.SetSize(vfes_truevsize);
   unm2 = 0.0;
   fn.SetSize(vfes_truevsize);
   Nun.SetSize(vfes_truevsize);
   Nun = 0.0;
   Nunm1.SetSize(vfes_truevsize);
   Nunm1 = 0.0;
   Nunm2.SetSize(vfes_truevsize);
   Nunm2 = 0.0;
   Fext.SetSize(vfes_truevsize);
   FText.SetSize(vfes_truevsize);
   Lext.SetSize(vfes_truevsize);
   resu.SetSize(vfes_truevsize);

   tmp1.SetSize(vfes_truevsize);

   pn.SetSize(pfes_truevsize);
   resp.SetSize(pfes_truevsize);
   FText_bdr.SetSize(pfes_truevsize);
   g_bdr.SetSize(pfes_truevsize);

   un_gf.SetSpace(vfes);
   un_gf = 0.0;
   un_next_gf.SetSpace(vfes);
   un_next_gf = 0.0;

   Lext_gf.SetSpace(vfes);
   curlu_gf.SetSpace(vfes);
   curlcurlu_gf.SetSpace(vfes);
   FText_gf.SetSpace(vfes);
   resu_gf.SetSpace(vfes);

   pn_gf.SetSpace(pfes);
   pn_gf = 0.0;
   resp_gf.SetSpace(pfes);

   cur_step = 0;

   if (verbose)
   {
      PrintInfo();
   }
}

void NavierSolver::Setup(double dt)
{
   if (verbose && pmesh->GetMyRank() == 0)
   {
      std::cout << "Setup" << std::endl;
      if (partial_assembly)
      {
         std::cout << "Using Partial Assembly" << std::endl;
      }
      else
      {
         std::cout << "Using FULL Assembly" << std::endl;
      }
   }

   sw_setup.Start();

   pmesh_lor = new ParMesh(pmesh, order, BasisType::GaussLobatto);
   pfec_lor = new H1_FECollection(1);
   pfes_lor = new ParFiniteElementSpace(pmesh_lor, pfec_lor);

   vfes->GetEssentialTrueDofs(vel_ess_attr, vel_ess_tdof);
   pfes->GetEssentialTrueDofs(pres_ess_attr, pres_ess_tdof);

   Array<int> empty;

   // GLL integration rule (Numerical Integration)
   const IntegrationRule &ir_ni = rules_ni.Get(vfes->GetFE(0)->GetGeomType(),
                                               2 * order - 1);

   nlcoeff.constant = -1.0;
   N = new ParNonlinearForm(vfes);
   N->AddDomainIntegrator(new VectorConvectionNLFIntegrator(nlcoeff));
   if (partial_assembly)
   {
      N->SetAssemblyLevel(AssemblyLevel::PARTIAL);
      N->Setup();
   }

   Mv_form = new ParBilinearForm(vfes);
   BilinearFormIntegrator *mv_blfi = new VectorMassIntegrator;
   if (numerical_integ)
   {
      mv_blfi->SetIntRule(&ir_ni);
   }
   Mv_form->AddDomainIntegrator(mv_blfi);
   if (partial_assembly)
   {
      Mv_form->SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }
   Mv_form->Assemble();
   Mv_form->FormSystemMatrix(empty, Mv);

   Sp_form = new ParBilinearForm(pfes);
   BilinearFormIntegrator *sp_blfi = new DiffusionIntegrator;
   if (numerical_integ)
   {
      sp_blfi->SetIntRule(&ir_ni);
   }
   Sp_form->AddDomainIntegrator(sp_blfi);
   if (partial_assembly)
   {
      Sp_form->SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }
   Sp_form->Assemble();
   Sp_form->FormSystemMatrix(pres_ess_tdof, Sp);

   D_form = new ParMixedBilinearForm(vfes, pfes);
   BilinearFormIntegrator *d_blfi = new VectorDivergenceIntegrator;
   if (numerical_integ)
   {
      d_blfi->SetIntRule(&ir_ni);
   }
   D_form->AddDomainIntegrator(d_blfi);
   if (partial_assembly)
   {
      D_form->SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }
   D_form->Assemble();
   D_form->FormRectangularSystemMatrix(empty, empty, D);

   G_form = new ParMixedBilinearForm(pfes, vfes);
   BilinearFormIntegrator *g_blfi = new GradientIntegrator;
   if (numerical_integ)
   {
      g_blfi->SetIntRule(&ir_ni);
   }
   G_form->AddDomainIntegrator(g_blfi);
   if (partial_assembly)
   {
      G_form->SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }
   G_form->Assemble();
   G_form->FormRectangularSystemMatrix(empty, empty, G);

   H_lincoeff.constant = kin_vis;
   H_bdfcoeff.constant = 1.0 / dt;
   H_form = new ParBilinearForm(vfes);
   BilinearFormIntegrator *hvm_blfi = new VectorMassIntegrator(H_bdfcoeff);
   BilinearFormIntegrator *hvd_blfi = new VectorDiffusionIntegrator(H_lincoeff);
   if (numerical_integ)
   {
      hvm_blfi->SetIntRule(&ir_ni);
      hvd_blfi->SetIntRule(&ir_ni);
   }
   H_form->AddDomainIntegrator(hvm_blfi);
   H_form->AddDomainIntegrator(hvd_blfi);
   if (partial_assembly)
   {
      H_form->SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }
   H_form->Assemble();
   H_form->FormSystemMatrix(vel_ess_tdof, H);

   // TODO has to be changed for multiple boundary attribute definitions!
   // Assuming we only set one function for dirichlet on the whole boundary.
   // FText_bdr_form has to be set only on the attributes where there are velocity dirichlet bcs.
   // Maybe use vel_ess_attr?
   // Needs github PR #936 https://github.com/mfem/mfem/pull/936
   FText_gfcoeff = new VectorGridFunctionCoefficient(&FText_gf);
   FText_bdr_form = new ParLinearForm(pfes);
   FText_bdr_form->AddBoundaryIntegrator(new BoundaryNormalLFIntegrator(
                                            *FText_gfcoeff),
                                         vel_ess_attr);

   g_bdr_form = new ParLinearForm(pfes);
   for (auto vdbc = vel_dbcs.begin(); vdbc != vel_dbcs.end(); ++vdbc)
   {
      g_bdr_form->AddBoundaryIntegrator(new BoundaryNormalLFIntegrator(
                                           vdbc->coeff),
                                        vdbc->attr);
   }

   f_form = new ParLinearForm(vfes);
   for (auto acc = accel_terms.begin(); acc != accel_terms.end(); ++acc)
   {
      VectorDomainLFIntegrator *vdlfi = new VectorDomainLFIntegrator(acc->coeff);
      // TODO this order should always be the same as the nonlinear forms one!
      // const IntegrationRule &ir = IntRules.Get(vfes->GetFE(0)->GetGeomType(),
      //                                          4 * order);
      // vdlfi->SetIntRule(&ir);
      f_form->AddDomainIntegrator(vdlfi);
   }

   if (partial_assembly)
   {
      Vector diag_pa(vfes->GetTrueVSize());
      Mv_form->AssembleDiagonal(diag_pa);
      MvInvPC = new OperatorJacobiSmoother(diag_pa, empty);
   }
   else
   {
      MvInvPC = new HypreSmoother(*Mv.As<HypreParMatrix>());
      static_cast<HypreSmoother *>(MvInvPC)->SetType(HypreSmoother::Jacobi, 1);
   }
   MvInv = new CGSolver(MPI_COMM_WORLD);
   MvInv->iterative_mode = false;
   MvInv->SetOperator(*Mv);
   MvInv->SetPreconditioner(*MvInvPC);
   MvInv->SetPrintLevel(pl_mvsolve);
   MvInv->SetRelTol(1e-12);
   MvInv->SetMaxIter(200);

   if (partial_assembly)
   {
      Sp_form_lor = new ParBilinearForm(pfes_lor);
      Sp_form_lor->SetExternBFS(1);
      CopyDBFIntegrators(Sp_form, Sp_form_lor);
      Sp_form_lor->Assemble();
      Sp_form_lor->FormSystemMatrix(pres_ess_tdof, Sp_lor);
      SpInvPC = new HypreBoomerAMG(*Sp_lor.As<HypreParMatrix>());
      SpInvPC->SetPrintLevel(pl_amg);
      SpInvPC->Mult(resp, pn);
      SpInvOrthoPC = new OrthoSolver();
      SpInvOrthoPC->SetOperator(*SpInvPC);
   }
   else
   {
      SpInvPC = new HypreBoomerAMG(*Sp.As<HypreParMatrix>());
      SpInvPC->SetPrintLevel(0);
      SpInvOrthoPC = new OrthoSolver();
      SpInvOrthoPC->SetOperator(*SpInvPC);
   }
   SpInv = new CGSolver(MPI_COMM_WORLD);
   SpInv->iterative_mode = true;
   SpInv->SetOperator(*Sp);
   if (pres_dbcs.empty())
   {
      SpInv->SetPreconditioner(*SpInvOrthoPC);
   }
   else
   {
      SpInv->SetPreconditioner(*SpInvPC);
   }
   SpInv->SetPrintLevel(pl_spsolve);
   SpInv->SetRelTol(rtol_spsolve);
   SpInv->SetMaxIter(200);

   if (partial_assembly)
   {
      Vector diag_pa(vfes->GetTrueVSize());
      H_form->AssembleDiagonal(diag_pa);
      HInvPC = new OperatorJacobiSmoother(diag_pa, vel_ess_tdof);
   }
   else
   {
      HInvPC = new HypreSmoother(*H.As<HypreParMatrix>());
      static_cast<HypreSmoother *>(HInvPC)->SetType(HypreSmoother::Jacobi, 1);
   }
   HInv = new CGSolver(MPI_COMM_WORLD);
   HInv->iterative_mode = true;
   HInv->SetOperator(*H);
   HInv->SetPreconditioner(*HInvPC);
   HInv->SetPrintLevel(pl_hsolve);
   HInv->SetRelTol(rtol_hsolve);
   HInv->SetMaxIter(200);

   // Set initial time step in history array
   dthist[0] = dt;

   sw_setup.Stop();
}

void NavierSolver::UpdateTimestepHistory(double dt)
{
  // Rotate values in time step history array
  dthist[2] = dthist[1];
  dthist[1] = dthist[0];
  dthist[0] = dt;

  // Rotate values in nonlinear extrapolation history array
  Nunm2 = Nunm1;
  Nunm1 = Nun;

  // Rotate values in solution history array
  unm2 = unm1;
  unm1 = un;

  // Update the current solution and corresponding GridFunction
  un = un_next;
  un_gf.SetFromTrueDofs(un);
}

void NavierSolver::ProvisionalStep(double time, double dt, int cur_step)
{
   if (verbose && pmesh->GetMyRank() == 0)
   {
      std::cout << "Step " << cur_step << std::endl;
   }
   sw_step.Start();
   sw_single_step.Start();

   for (auto vdbc = vel_dbcs.begin(); vdbc != vel_dbcs.end(); ++vdbc)
   {
      vdbc->coeff.SetTime(time + dt);
   }

   for (auto pdbc = pres_dbcs.begin(); pdbc != pres_dbcs.end(); ++pdbc)
   {
      pdbc->coeff.SetTime(time + dt);
   }

   SetTimeIntegrationCoefficients(cur_step);

   H_bdfcoeff.constant = bd0 / dt;
   H_form->Update();
   H_form->Assemble();
   H_form->FormSystemMatrix(vel_ess_tdof, H);

   if (partial_assembly)
   {
     HInv->ClearPreconditioner();
     HInv->SetOperator(*H);
     delete HInvPC;
     Vector diag_pa(vfes->GetTrueVSize());
     H_form->AssembleDiagonal(diag_pa);
     HInvPC = new OperatorJacobiSmoother(diag_pa, vel_ess_tdof);
     HInv->SetPreconditioner(*HInvPC);
   }
   else
   {
     HInv->SetOperator(*H);
   }

   for (auto acc = accel_terms.begin(); acc != accel_terms.end(); ++acc)
   {
      acc->coeff.SetTime(time);
   }

   f_form->Assemble();
   f_form->ParallelAssemble(fn);
   
   //
   // Nonlinear EXT terms
   //

   sw_extrap.Start();

   N->Mult(un, Nun);
   Nun.Add(1.0, fn);

   MFEM_FORALL(i,
               Fext.Size(),
               Fext[i] = ab1 * Nun[i] + ab2 * Nunm1[i] + ab3 * Nunm2[i];);

   // Fext = M^{-1} (F(u^{n}) + f^{n+1})
   MvInv->Mult(Fext, tmp1);
   iter_mvsolve = MvInv->GetNumIterations();
   res_mvsolve = MvInv->GetFinalNorm();
   Fext.Set(1.0, tmp1);

   // BDF terms
   double bd1idt = -bd1 / dt;
   double bd2idt = -bd2 / dt;
   double bd3idt = -bd3 / dt;
   MFEM_FORALL(i,
               Fext.Size(),
               Fext[i] += bd1idt * un[i] + bd2idt * unm1[i] + bd3idt * unm2[i];);

   sw_extrap.Stop();

   //
   // Pressure poisson
   //

   sw_curlcurl.Start();

   MFEM_FORALL(i,
               Lext.Size(),
               Lext[i] = ab1 * un[i] + ab2 * unm1[i] + ab3 * unm2[i];);

   Lext_gf.SetFromTrueDofs(Lext);
   if (pmesh->Dimension() == 2)
   {
      ComputeCurl2D(Lext_gf, curlu_gf);
      ComputeCurl2D(curlu_gf, curlcurlu_gf, true);
   }
   else
   {
      ComputeCurl3D(Lext_gf, curlu_gf);
      ComputeCurl3D(curlu_gf, curlcurlu_gf);
   }

   curlcurlu_gf.GetTrueDofs(Lext);
   Lext *= kin_vis;

   sw_curlcurl.Stop();

   // \tilde{F} = F - \nu CurlCurl(u)
   FText.Set(-1.0, Lext);
   FText.Add(1.0, Fext);

   // p_r = \nabla \cdot FText
   D->Mult(FText, resp);
   resp.Neg();

   // Add boundary terms
   FText_gf.SetFromTrueDofs(FText);
   FText_bdr_form->Assemble();
   FText_bdr_form->ParallelAssemble(FText_bdr);

   g_bdr_form->Assemble();
   g_bdr_form->ParallelAssemble(g_bdr);
   resp.Add(1.0, FText_bdr);
   resp.Add(-bd0 / dt, g_bdr);

   if (pres_dbcs.empty())
   {
      Orthogonalize(resp);
   }

   for (auto pdbc = pres_dbcs.begin(); pdbc != pres_dbcs.end(); ++pdbc)
   {
      pn_gf.ProjectBdrCoefficient(pdbc->coeff, pdbc->attr);
   }

   pfes->GetRestrictionMatrix()->MultTranspose(resp, resp_gf);

   Vector X1, B1;
   if (partial_assembly)
   {
      ConstrainedOperator *SpC = Sp.As<ConstrainedOperator>();
      EliminateRHS(*Sp_form, *SpC, pres_ess_tdof, pn_gf, resp_gf, X1, B1, 1);
   }
   else
   {
      Sp_form->FormLinearSystem(pres_ess_tdof, pn_gf, resp_gf, Sp, X1, B1, 1);
   }
   sw_spsolve.Start();
   SpInv->Mult(B1, X1);
   sw_spsolve.Stop();
   iter_spsolve = SpInv->GetNumIterations();
   res_spsolve = SpInv->GetFinalNorm();
   Sp_form->RecoverFEMSolution(X1, resp_gf, pn_gf);

   if (pres_dbcs.empty())
   {
      MeanZero(pn_gf);
   }

   pn_gf.GetTrueDofs(pn);

   //
   // Project velocity
   //

   G->Mult(pn, resu);
   resu.Neg();
   Mv->Mult(Fext, tmp1);
   resu.Add(1.0, tmp1);

   for (auto vdbc = vel_dbcs.begin(); vdbc != vel_dbcs.end(); ++vdbc)
   {
      un_next_gf.ProjectBdrCoefficient(vdbc->coeff, vdbc->attr);
   }

   vfes->GetRestrictionMatrix()->MultTranspose(resu, resu_gf);

   Vector X2, B2;
   if (partial_assembly)
   {
      ConstrainedOperator *HC = H.As<ConstrainedOperator>();
      EliminateRHS(*H_form, *HC, vel_ess_tdof, un_next_gf, resu_gf, X2, B2, 1);
   }
   else
   {
      H_form->FormLinearSystem(vel_ess_tdof, un_next_gf, resu_gf, H, X2, B2, 1);
   }
   sw_hsolve.Start();
   HInv->Mult(B2, X2);
   sw_hsolve.Stop();
   iter_hsolve = HInv->GetNumIterations();
   res_hsolve = HInv->GetFinalNorm();
   H_form->RecoverFEMSolution(X2, resu_gf, un_next_gf);

   un_next_gf.GetTrueDofs(un_next);

   sw_step.Stop();
   sw_single_step.Stop();

   if (verbose && pmesh->GetMyRank() == 0)
   {
      if (!numerical_integ)
      {
         printf("MVIN %3d %.2E %.2E\n", iter_mvsolve, res_mvsolve, 1e-12);
      }
      printf("PRES %3d %.2E %.2E\n", iter_spsolve, res_spsolve, rtol_spsolve);
      printf("HELM %3d %.2E %.2E\n", iter_hsolve, res_hsolve, rtol_hsolve);
      printf("TPS %22.2E\n", sw_single_step.RealTime());
   }

   sw_single_step.Clear();
}

void NavierSolver::Step(double time, double dt, int cur_step)
{
  ProvisionalStep(time, dt, cur_step);
  UpdateTimestepHistory(dt);
}

void NavierSolver::MeanZero(ParGridFunction &v)
{
   if (mass_lf == nullptr)
   {
      onecoeff.constant = 1.0;
      mass_lf = new ParLinearForm(v.ParFESpace());
      mass_lf->AddDomainIntegrator(new DomainLFIntegrator(onecoeff));
      mass_lf->Assemble();

      ParGridFunction one_gf(v.ParFESpace());
      one_gf.ProjectCoefficient(onecoeff);

      volume = mass_lf->operator()(one_gf);
   }

   double integ = mass_lf->operator()(v);

   v -= integ / volume;
}

void NavierSolver::EliminateRHS(Operator &A,
                                ConstrainedOperator &constrainedA,
                                const Array<int> &ess_tdof_list,
                                Vector &x,
                                Vector &b,
                                Vector &X,
                                Vector &B,
                                int copy_interior)
{
   const Operator *P = A.GetProlongation();
   const Operator *R = A.GetRestriction();
   A.InitTVectors(P, R, x, b, X, B);
   if (!copy_interior)
   {
      X.SetSubVectorComplement(ess_tdof_list, 0.0);
   }
   constrainedA.EliminateRHS(X, B);
}

void NavierSolver::Orthogonalize(Vector &v)
{
   double loc_sum = v.Sum();
   double global_sum = 0.0;
   int loc_size = v.Size();
   int global_size = 0;

   MPI_Allreduce(&loc_sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
   MPI_Allreduce(&loc_size, &global_size, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

   v -= global_sum / static_cast<double>(global_size);
}

void NavierSolver::ComputeCurl3D(ParGridFunction &u, ParGridFunction &cu)
{
   FiniteElementSpace *fes = u.FESpace();

   // AccumulateAndCountZones
   Array<int> zones_per_vdof;
   zones_per_vdof.SetSize(fes->GetVSize());
   zones_per_vdof = 0;

   cu = 0.0;

   // Local interpolation
   int elndofs;
   Array<int> vdofs;
   Vector vals;
   Vector loc_data;
   int vdim = fes->GetVDim();
   DenseMatrix grad_hat;
   DenseMatrix dshape;
   DenseMatrix grad;
   Vector curl;

   for (int e = 0; e < fes->GetNE(); ++e)
   {
      fes->GetElementVDofs(e, vdofs);
      u.GetSubVector(vdofs, loc_data);
      vals.SetSize(vdofs.Size());
      ElementTransformation *tr = fes->GetElementTransformation(e);
      const FiniteElement *el = fes->GetFE(e);
      elndofs = el->GetDof();
      int dim = el->GetDim();
      dshape.SetSize(elndofs, dim);

      for (int dof = 0; dof < elndofs; ++dof)
      {
         // Project
         const IntegrationPoint &ip = el->GetNodes().IntPoint(dof);
         tr->SetIntPoint(&ip);

         // Eval
         // GetVectorGradientHat
         el->CalcDShape(tr->GetIntPoint(), dshape);
         grad_hat.SetSize(vdim, dim);
         DenseMatrix loc_data_mat(loc_data.GetData(), elndofs, vdim);
         MultAtB(loc_data_mat, dshape, grad_hat);

         const DenseMatrix &Jinv = tr->InverseJacobian();
         grad.SetSize(grad_hat.Height(), Jinv.Width());
         Mult(grad_hat, Jinv, grad);

         curl.SetSize(3);
         curl(0) = grad(2, 1) - grad(1, 2);
         curl(1) = grad(0, 2) - grad(2, 0);
         curl(2) = grad(1, 0) - grad(0, 1);

         for (int j = 0; j < curl.Size(); ++j)
         {
            vals(elndofs * j + dof) = curl(j);
         }
      }

      // Accumulate values in all dofs, count the zones.
      for (int j = 0; j < vdofs.Size(); j++)
      {
         int ldof = vdofs[j];
         cu(ldof) += vals[j];
         zones_per_vdof[ldof]++;
      }
   }

   // Communication

   // Count the zones globally.
   GroupCommunicator &gcomm = u.ParFESpace()->GroupComm();
   gcomm.Reduce<int>(zones_per_vdof, GroupCommunicator::Sum);
   gcomm.Bcast(zones_per_vdof);

   // Accumulate for all vdofs.
   gcomm.Reduce<double>(cu.GetData(), GroupCommunicator::Sum);
   gcomm.Bcast<double>(cu.GetData());

   // Compute means
   for (int i = 0; i < cu.Size(); i++)
   {
      const int nz = zones_per_vdof[i];
      if (nz)
      {
         cu(i) /= nz;
      }
   }
}

void NavierSolver::ComputeCurl2D(ParGridFunction &u,
                                 ParGridFunction &cu,
                                 bool assume_scalar)
{
   FiniteElementSpace *fes = u.FESpace();

   // AccumulateAndCountZones
   Array<int> zones_per_vdof;
   zones_per_vdof.SetSize(fes->GetVSize());
   zones_per_vdof = 0;

   cu = 0.0;

   // Local interpolation
   int elndofs;
   Array<int> vdofs;
   Vector vals;
   Vector loc_data;
   int vdim = fes->GetVDim();
   DenseMatrix grad_hat;
   DenseMatrix dshape;
   DenseMatrix grad;
   Vector curl;

   for (int e = 0; e < fes->GetNE(); ++e)
   {
      fes->GetElementVDofs(e, vdofs);
      u.GetSubVector(vdofs, loc_data);
      vals.SetSize(vdofs.Size());
      ElementTransformation *tr = fes->GetElementTransformation(e);
      const FiniteElement *el = fes->GetFE(e);
      elndofs = el->GetDof();
      int dim = el->GetDim();
      dshape.SetSize(elndofs, dim);

      for (int dof = 0; dof < elndofs; ++dof)
      {
         // Project
         const IntegrationPoint &ip = el->GetNodes().IntPoint(dof);
         tr->SetIntPoint(&ip);

         // Eval
         // GetVectorGradientHat
         el->CalcDShape(tr->GetIntPoint(), dshape);
         grad_hat.SetSize(vdim, dim);
         DenseMatrix loc_data_mat(loc_data.GetData(), elndofs, vdim);
         MultAtB(loc_data_mat, dshape, grad_hat);

         const DenseMatrix &Jinv = tr->InverseJacobian();
         grad.SetSize(grad_hat.Height(), Jinv.Width());
         Mult(grad_hat, Jinv, grad);

         if (assume_scalar)
         {
            curl.SetSize(2);
            curl(0) = grad(0, 1);
            curl(1) = -grad(0, 0);
         }
         else
         {
            curl.SetSize(2);
            curl(0) = grad(1, 0) - grad(0, 1);
            curl(1) = 0.0;
         }

         for (int j = 0; j < curl.Size(); ++j)
         {
            vals(elndofs * j + dof) = curl(j);
         }
      }

      // Accumulate values in all dofs, count the zones.
      for (int j = 0; j < vdofs.Size(); j++)
      {
         int ldof = vdofs[j];
         cu(ldof) += vals[j];
         zones_per_vdof[ldof]++;
      }
   }

   // Communication

   // Count the zones globally.
   GroupCommunicator &gcomm = u.ParFESpace()->GroupComm();
   gcomm.Reduce<int>(zones_per_vdof, GroupCommunicator::Sum);
   gcomm.Bcast(zones_per_vdof);

   // Accumulate for all vdofs.
   gcomm.Reduce<double>(cu.GetData(), GroupCommunicator::Sum);
   gcomm.Bcast<double>(cu.GetData());

   // Compute means
   for (int i = 0; i < cu.Size(); i++)
   {
      const int nz = zones_per_vdof[i];
      if (nz)
      {
         cu(i) /= nz;
      }
   }
}

void NavierSolver::AddVelDirichletBC(VecFuncT *f, Array<int> &attr)
{
   vel_dbcs.push_back(
      VelDirichletBC_T(f,
                       attr,
                       VectorFunctionCoefficient(pmesh->Dimension(), f)));

   if (verbose && pmesh->GetMyRank() == 0)
   {
      out << "Adding Velocity Dirichlet BC to attributes ";
      for (int i = 0; i < attr.Size(); ++i)
      {
         if (attr[i] == 1)
         {
            out << i << " ";
         }
      }
      out << std::endl;
   }

   for (int i = 0; i < attr.Size(); ++i)
   {
      MFEM_ASSERT((vel_ess_attr[i] && attr[i]) == 0,
                  "Duplicate boundary definition deteceted.");
      if (attr[i] == 1)
      {
         vel_ess_attr[i] = 1;
      }
   }
}

void NavierSolver::AddPresDirichletBC(ScalarFuncT *f, Array<int> &attr)
{
   pres_dbcs.push_back(PresDirichletBC_T(f, attr, FunctionCoefficient(f)));

   if (verbose && pmesh->GetMyRank() == 0)
   {
      out << "Adding Pressure Dirichlet BC to attributes ";
      for (int i = 0; i < attr.Size(); ++i)
      {
         if (attr[i] == 1)
         {
            out << i << " ";
         }
      }
      out << std::endl;
   }

   for (int i = 0; i < attr.Size(); ++i)
   {
      MFEM_ASSERT((pres_ess_attr[i] && attr[i]) == 0,
                  "Duplicate boundary definition deteceted.");
      if (attr[i] == 1)
      {
         pres_ess_attr[i] = 1;
      }
   }
}

void NavierSolver::AddAccelTerm(VecFuncT *f, Array<int> &attr)
{
   accel_terms.push_back(
      AccelTerm_T(f, attr, VectorFunctionCoefficient(pmesh->Dimension(), f)));

   if (verbose && pmesh->GetMyRank() == 0)
   {
      out << "Adding Acceleration term to attributes ";
      for (int i = 0; i < attr.Size(); ++i)
      {
         if (attr[i] == 1)
         {
            out << i << " ";
         }
      }
      out << std::endl;
   }
}

void NavierSolver::SetTimeIntegrationCoefficients(int step)
{
   // Maxmium BDF order to use at current time step
   // step + 1 <= order <= max_bdf_order
   int bdf_order = std::min(step + 1, max_bdf_order);

   // Ratio of time step history at dt(t{n}) - dt(t_{n-1})
   double rho1 = 0.0;
   // Ratio of time step history at dt(t{n-1}) - dt(t_{n-2})
   double rho2 = 0.0;

   if (bdf_order >= 1)
   {
      rho1 = dthist[0] / dthist[1];
   }

   if (bdf_order == 3)
   {
      rho2 = dthist[1] / dthist[2];
   }

   if (step == 0 && bdf_order == 1)
   {
      bd0 = 1.0;
      bd1 = -1.0;
      bd2 = 0.0;
      bd3 = 0.0;
      ab1 = 1.0;
      ab2 = 0.0;
      ab3 = 0.0;
   }
   else if (step >= 1 && bdf_order == 2)
   {
      bd0 = (1.0 + 2.0 * rho1) / (1.0 + rho1);
      bd1 = -(1.0 + rho1);
      bd2 = pow(rho1, 2.0) / (1.0 + rho1);
      bd3 = 0.0;
      ab1 = 1.0 + rho1;
      ab2 = -rho1;
      ab3 = 0.0;
   }
   else if (step >= 2 && bdf_order == 3)
   {
      bd0 = 1.0 + rho1 / (1.0 + rho1)
            + (rho2 * rho1) / (1.0 + rho2 * (1 + rho1));
      bd1 = -1.0 - rho1 - (rho2 * rho1 * (1.0 + rho1)) / (1.0 + rho2);
      bd2 = pow(rho1, 2.0) * (rho2 + 1.0 / (1.0 + rho1));
      bd3 = -(pow(rho2, 3.0) * pow(rho1, 2.0) * (1.0 + rho1))
            / ((1.0 + rho2) * (1.0 + rho2 + rho2 * rho1));
      ab1 = ((1.0 + rho1) * (1.0 + rho2 * (1.0 + rho1))) / (1.0 + rho2);
      ab2 = -rho1 * (1.0 + rho2 * (1.0 + rho1));
      ab3 = (pow(rho2, 2.0) * rho1 * (1.0 + rho1)) / (1.0 + rho2);
   }
}

double NavierSolver::ComputeCFL(ParGridFunction &u, double &dt)
{
   ParMesh *pmesh = u.ParFESpace()->GetParMesh();

   double hmin = 0.0;
   double hmin_loc = pmesh->GetElementSize(0, 1);

   for (int i = 1; i < pmesh->GetNE(); i++)
   {
      hmin_loc = std::min(pmesh->GetElementSize(i, 1), hmin_loc);
   }

   MPI_Allreduce(&hmin_loc, &hmin, 1, MPI_DOUBLE, MPI_MIN, pmesh->GetComm());

   int ndofs = u.ParFESpace()->GetNDofs();
   Vector uc(ndofs), vc(ndofs), wc(ndofs);

   // Only set z-component to zero because x and y are always present.
   wc = 0.0;

   for (int comp = 0; comp < u.ParFESpace()->GetVDim(); comp++)
   {
      for (int i = 0; i < ndofs; i++)
      {
         if (comp == 0)
         {
            uc(i) = u[u.ParFESpace()->DofToVDof(i, comp)];
         }
         else if (comp == 1)
         {
            vc(i) = u[u.ParFESpace()->DofToVDof(i, comp)];
         }
         else if (comp == 2)
         {
            wc(i) = u[u.ParFESpace()->DofToVDof(i, comp)];
         }
      }
   }

   double velmag_max_loc = 0.0;
   double velmag_max = 0.0;
   for (int i = 0; i < ndofs; i++)
   {
      velmag_max_loc = std::max(sqrt(pow(uc(i), 2.0) + pow(vc(i), 2.0)
                                     + pow(wc(i), 2.0)),
                                velmag_max_loc);
   }

   MPI_Allreduce(&velmag_max_loc,
                 &velmag_max,
                 1,
                 MPI_DOUBLE,
                 MPI_MAX,
                 pmesh->GetComm());

   return velmag_max * dt / hmin;
}

void NavierSolver::PrintTimingData()
{
   double my_rt[6], rt_max[6];

   my_rt[0] = sw_setup.RealTime();
   my_rt[1] = sw_step.RealTime();
   my_rt[2] = sw_extrap.RealTime();
   my_rt[3] = sw_curlcurl.RealTime();
   my_rt[4] = sw_spsolve.RealTime();
   my_rt[5] = sw_hsolve.RealTime();

   MPI_Reduce(my_rt, rt_max, 6, MPI_DOUBLE, MPI_MAX, 0, pmesh->GetComm());

   if (pmesh->GetMyRank() == 0)
   {
      printf("%10s %10s %10s %10s %10s %10s\n",
             "SETUP",
             "STEP",
             "EXTRAP",
             "CURLCURL",
             "PSOLVE",
             "HSOLVE");
      printf("%10.3f %10.3f %10.3f %10.3f %10.3f %10.3f\n",
             my_rt[0],
             my_rt[1],
             my_rt[2],
             my_rt[3],
             my_rt[4],
             my_rt[5]);
      printf("%10s %10.3f %10.3f %10.3f %10.3f %10.3f\n",
             " ",
             my_rt[1] / my_rt[1],
             my_rt[2] / my_rt[1],
             my_rt[3] / my_rt[1],
             my_rt[4] / my_rt[1],
             my_rt[5] / my_rt[1]);
   }
}

void NavierSolver::PrintInfo()
{
   int fes_size0 = vfes->GlobalVSize();
   int fes_size1 = pfes->GlobalVSize();

   if (pmesh->GetMyRank() == 0)
   {
      out << "NAVIER version: "
          << "00000" << std::endl
          << "MFEM version: " << MFEM_VERSION << std::endl
          << "MFEM GIT: " << MFEM_GIT_STRING << std::endl
          << "Velocity #DOFs: " << fes_size0 << std::endl
          << "Pressure #DOFs: " << fes_size1 << std::endl;
   }
}

NavierSolver::~NavierSolver()
{
   delete FText_gfcoeff;
   delete g_bdr_form;
   delete FText_bdr_form;
   delete mass_lf;
   delete Mv_form;
   delete N;
   delete Sp_form;
   delete D_form;
   delete G_form;
   delete HInvPC;
   delete HInv;
   delete H_form;
   delete SpInv;
   delete MvInvPC;
   if (partial_assembly)
   {
      delete Sp_form_lor;
      delete SpInvOrthoPC;
      delete SpInvPC;
   }
   delete pfes_lor;
   delete pfec_lor;
   delete pmesh_lor;
   delete MvInv;
   delete vfec;
   delete pfec;
   delete vfes;
   delete pfes;
}