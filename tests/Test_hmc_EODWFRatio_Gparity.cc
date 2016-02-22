    /*************************************************************************************

    Grid physics library, www.github.com/paboyle/Grid 

    Source file: ./tests/Test_hmc_EODWFRatio_Gparity.cc

    Copyright (C) 2015

Author: paboyle <paboyle@ph.ed.ac.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    See the full license in the file "LICENSE" in the top level distribution directory
    *************************************************************************************/
    /*  END LEGAL */
#include "Grid.h"

using namespace std;
using namespace Grid;
using namespace Grid::QCD;

namespace Grid { 
  namespace QCD { 


class HmcRunner : public ConjugateNerscHmcRunner {
public:

  void BuildTheAction (int argc, char **argv)

  {
    typedef GparityWilsonImplR ImplPolicy;
    typedef GparityDomainWallFermionR FermionAction;

    typedef typename FermionAction::FermionField FermionField;

    const int Ls = 8;

    UGrid   = SpaceTimeGrid::makeFourDimGrid(GridDefaultLatt(), GridDefaultSimd(Nd,vComplex::Nsimd()),GridDefaultMpi());
    UrbGrid = SpaceTimeGrid::makeFourDimRedBlackGrid(UGrid);
  
    FGrid   = SpaceTimeGrid::makeFiveDimGrid(Ls,UGrid);
    FrbGrid = SpaceTimeGrid::makeFiveDimRedBlackGrid(Ls,UGrid);

    // temporarily need a gauge field
    LatticeGaugeField  U(UGrid);

    // Gauge action
    ConjugateWilsonGaugeActionR Waction(5.6);

    // Fermion action
    const int nu = 3;
    std::vector<int> twists(Nd,0);
    twists[nu] = 1;
    FermionAction::ImplParams params;
    params.twists = twists;
    Real mass=0.04;
    Real pv  =1.0;
    RealD M5=1.5;
    FermionAction DenOp(U,*FGrid,*FrbGrid,*UGrid,*UrbGrid,mass,M5,params);
    FermionAction NumOp(U,*FGrid,*FrbGrid,*UGrid,*UrbGrid,pv,M5,params);
  
    ConjugateGradient<FermionField>  CG(1.0e-8,10000);
    TwoFlavourEvenOddRatioPseudoFermionAction<ImplPolicy> Nf2(NumOp, DenOp,CG,CG);
  
    //Collect actions
    ActionLevel<LatticeGaugeField> Level1;
    Level1.push_back(&Nf2);
    Level1.push_back(&Waction);
    TheAction.push_back(Level1);

    Run(argc,argv);
  };

};

}}

int main (int argc, char ** argv)
{
  Grid_init(&argc,&argv);

  int threads = GridThread::GetThreads();
  std::cout<<GridLogMessage << "Grid is setup to use "<<threads<<" threads"<<std::endl;

  HmcRunner TheHMC;
  
  TheHMC.BuildTheAction(argc,argv);

}
