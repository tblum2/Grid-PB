/*************************************************************************************
    Grid physics library, www.github.com/paboyle/Grid 

    Source file: ./lib/lattice/PaddedCell.h

    Copyright (C) 2019

Author: Peter Boyle pboyle@bnl.gov

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
#pragma once

#include<Grid/cshift/Cshift.h>

NAMESPACE_BEGIN(Grid);

//Allow the user to specify how the C-shift is performed, e.g. to respect the appropriate boundary conditions
template<typename vobj>
struct CshiftImplBase{
  virtual Lattice<vobj> Cshift(const Lattice<vobj> &in, int dir, int shift) const = 0;
  virtual ~CshiftImplBase(){}
};
template<typename vobj>
struct CshiftImplDefault: public CshiftImplBase<vobj>{
  Lattice<vobj> Cshift(const Lattice<vobj> &in, int dir, int shift) const override{ return Grid::Cshift(in,dir,shift); }
};
template<typename Gimpl>
struct CshiftImplGauge: public CshiftImplBase<typename Gimpl::GaugeLinkField::vector_object>{
  typename Gimpl::GaugeLinkField Cshift(const typename Gimpl::GaugeLinkField &in, int dir, int shift) const override{ return Gimpl::CshiftLink(in,dir,shift); }
};  



template<class vobj> inline void ScatterSlice(const cshiftVector<typename vobj::scalar_object> &buf,
					      Lattice<vobj> &lat,
					      int x,
					      int dim,
					      int offset=0)
{
  typedef typename vobj::scalar_object sobj;

  autoView(lat_v, lat, AcceleratorRead);

  GridBase *grid = lat.Grid();
  Coordinate simd = grid->_simd_layout;
  int Nd          = grid->Nd();
  int block       = grid->_slice_block[dim];
  int stride      = grid->_slice_stride[dim];
  int nblock      = grid->_slice_nblock[dim];
  int rd          = grid->_rdimensions[dim];

  int ox = x%rd;
  int ix = x/rd;

  int isites = 1; for(int d=0;d<Nd;d++) if( d!=dim) isites*=simd[d];

  Coordinate rsimd= simd;  rsimd[dim]=1; // maybe reduce Nsimd

  int rNsimd = 1; for(int d=0;d<Nd;d++) rNsimd*=rsimd[d];
  
  int face_ovol=block*nblock;

  //  assert(buf.size()==face_ovol*rNsimd);

  /*This will work GPU ONLY unless rNsimd is put in the lexico index*/
  //Let's make it work on GPU and then make a special accelerator_for that
  //doesn't hide the SIMD direction and keeps explicit in the threadIdx
  //for cross platform
  auto buf_p = & buf[0];
  accelerator_for(ss, face_ovol,rNsimd,{

    // scalar layout won't coalesce
    int olane=acceleratorSIMTlane(rNsimd);
    sobj obj = buf_p[ss+olane*face_ovol+offset];

    ////////////////////////////////////////////
    // osite
    ////////////////////////////////////////////
    int b    = ss%block;
    int n    = ss/block;
    int osite= b+n*stride + ox*block;

    ////////////////////////////////////////////
    // isite
    ////////////////////////////////////////////
    Coordinate icoor;
    int lane;
    Lexicographic::CoorFromIndex(icoor,olane,rsimd);
    icoor[dim]=ix;
    Lexicographic::IndexFromCoor(icoor,lane,simd);

    ///////////////////////////////////////////
    // Transfer into lattice - will coalesce
    ///////////////////////////////////////////
    insertLane(lane,lat_v[osite],obj);
  });
}

template<class vobj> inline void GatherSlice(cshiftVector<typename vobj::scalar_object> &buf,
					     const Lattice<vobj> &lat,
					     int x,
					     int dim,
					     int offset=0)
{
  typedef typename vobj::scalar_object sobj;

  autoView(lat_v, lat, AcceleratorRead);

  GridBase *grid = lat.Grid();
  Coordinate simd = grid->_simd_layout;
  int Nd          = grid->Nd();
  int block       = grid->_slice_block[dim];
  int stride      = grid->_slice_stride[dim];
  int nblock      = grid->_slice_nblock[dim];
  int rd          = grid->_rdimensions[dim];

  int ox = x%rd;
  int ix = x/rd;

  int isites = 1; for(int d=0;d<Nd;d++) if( d!=dim) isites*=simd[d];

  Coordinate rsimd= simd;  rsimd[dim]=1; // maybe reduce Nsimd

  int rNsimd = 1; for(int d=0;d<Nd;d++) rNsimd*=rsimd[d];
  
  int face_ovol=block*nblock;

  //  assert(buf.size()==face_ovol*rNsimd);

  /*This will work GPU ONLY unless rNsimd is put in the lexico index*/
  //Let's make it work on GPU and then make a special accelerator_for that
  //doesn't hide the SIMD direction and keeps explicit in the threadIdx
  //for cross platform
  //For CPU perhaps just run a loop over Nsimd
  auto buf_p = & buf[0];
  accelerator_for(ss, face_ovol,rNsimd,{

    ////////////////////////////////////////////
    // osite
    ////////////////////////////////////////////
    int b    = ss%block;
    int n    = ss/block;
    int osite= b+n*stride + ox*block;

    ////////////////////////////////////////////
    // isite
    ////////////////////////////////////////////
    Coordinate icoor;
    int olane=acceleratorSIMTlane(rNsimd);
    int lane;
    Lexicographic::CoorFromIndex(icoor,olane,rsimd);
    icoor[dim]=ix;
    Lexicographic::IndexFromCoor(icoor,lane,simd);

    ///////////////////////////////////////////
    // Take out of lattice
    ///////////////////////////////////////////
    sobj obj = extractLane(lane,lat_v[osite]);
    buf_p[ss+olane*face_ovol+offset] = obj;

  });
}


class PaddedCell {
public:
  GridCartesian * unpadded_grid;
  int dims;
  int depth;
  std::vector<GridCartesian *> grids;

  ~PaddedCell()
  {
    DeleteGrids();
  }
  PaddedCell(int _depth,GridCartesian *_grid)
  {
    unpadded_grid = _grid;
    depth=_depth;
    dims=_grid->Nd();
    AllocateGrids();
    Coordinate local     =unpadded_grid->LocalDimensions();
    Coordinate procs     =unpadded_grid->ProcessorGrid();
    for(int d=0;d<dims;d++){
      if ( procs[d] > 1 ) assert(local[d]>=depth);
    }
  }
  void DeleteGrids(void)
  {
    for(int d=0;d<grids.size();d++){
      delete grids[d];
    }
    grids.resize(0);
  };
  void AllocateGrids(void)
  {
    Coordinate local     =unpadded_grid->LocalDimensions();
    Coordinate simd      =unpadded_grid->_simd_layout;
    Coordinate processors=unpadded_grid->_processors;
    Coordinate plocal    =unpadded_grid->LocalDimensions();
    Coordinate global(dims);
    GridCartesian *old_grid = unpadded_grid;
    // expand up one dim at a time
    for(int d=0;d<dims;d++){

      if ( processors[d] > 1 ) { 
	plocal[d] += 2*depth; 
      
	for(int d=0;d<dims;d++){
	  global[d] = plocal[d]*processors[d];
	}

	old_grid = new GridCartesian(global,simd,processors);
      }
      grids.push_back(old_grid);
    }
  };
  template<class vobj>
  inline Lattice<vobj> Extract(const Lattice<vobj> &in) const
  {
    Coordinate processors=unpadded_grid->_processors;

    Lattice<vobj> out(unpadded_grid);

    Coordinate local     =unpadded_grid->LocalDimensions();
    // depends on the MPI spread      
    Coordinate fll(dims,depth);
    Coordinate tll(dims,0); // depends on the MPI spread
    for(int d=0;d<dims;d++){
      if( processors[d]==1 ) fll[d]=0;
    }
    localCopyRegion(in,out,fll,tll,local);
    return out;
  }
  template<class vobj>
  inline Lattice<vobj> Exchange(const Lattice<vobj> &in, const CshiftImplBase<vobj> &cshift = CshiftImplDefault<vobj>()) const
  {
    GridBase *old_grid = in.Grid();
    int dims = old_grid->Nd();
    Lattice<vobj> tmp = in;
    for(int d=0;d<dims;d++){
      tmp = Expand(d,tmp,cshift); // rvalue && assignment
    }
    return tmp;
  }
  template<class vobj>
  inline Lattice<vobj> ExchangeTest(const Lattice<vobj> &in, const CshiftImplBase<vobj> &cshift = CshiftImplDefault<vobj>()) const
  {
    GridBase *old_grid = in.Grid();
    int dims = old_grid->Nd();
    Lattice<vobj> tmp = in;
    for(int d=0;d<dims;d++){
      tmp = ExpandTest(d,tmp,cshift); // rvalue && assignment
    }
    return tmp;
  }
  // expand up one dim at a time
  template<class vobj>
  inline Lattice<vobj> Expand(int dim, const Lattice<vobj> &in, const CshiftImplBase<vobj> &cshift = CshiftImplDefault<vobj>()) const
  {
    Coordinate processors=unpadded_grid->_processors;
    GridBase *old_grid = in.Grid();
    GridCartesian *new_grid = grids[dim];//These are new grids
    Lattice<vobj>  padded(new_grid);
    Lattice<vobj> shifted(old_grid);    
    Coordinate local     =old_grid->LocalDimensions();
    Coordinate plocal    =new_grid->LocalDimensions();
    if(dim==0) conformable(old_grid,unpadded_grid);
    else       conformable(old_grid,grids[dim-1]);

    //    std::cout << " dim "<<dim<<" local "<<local << " padding to "<<plocal<<std::endl;

    double tins=0, tshift=0;

    int islocal = 0 ;
    if ( processors[dim] == 1 ) islocal = 1;

    if ( islocal ) {

      // replace with a copy and maybe grid swizzle
      double t = usecond();
      padded = in;
      tins += usecond() - t;
      
    } else {

      //////////////////////////////////////////////
      // Replace sequence with
      // ---------------------
      // (i) Gather high face(s); start comms
      // (ii) Gather low  face(s); start comms
      // (iii) Copy middle bit with localCopyRegion
      // (iv) Complete high face(s), insert slice(s)
      // (iv) Complete low  face(s), insert slice(s)
      //////////////////////////////////////////////
      // Middle bit
      double t = usecond();
      for(int x=0;x<local[dim];x++){
	InsertSliceLocal(in,padded,x,depth+x,dim);
      }
      tins += usecond() - t;
    
      // High bit
      t = usecond();
      shifted = cshift.Cshift(in,dim,depth);
      tshift += usecond() - t;

      t=usecond();
      for(int x=0;x<depth;x++){
	InsertSliceLocal(shifted,padded,local[dim]-depth+x,depth+local[dim]+x,dim);
      }
      tins += usecond() - t;
    
      // Low bit
      t = usecond();
      shifted = cshift.Cshift(in,dim,-depth);
      tshift += usecond() - t;
    
      t = usecond();
      for(int x=0;x<depth;x++){
	InsertSliceLocal(shifted,padded,x,x,dim);
      }
      tins += usecond() - t;
      //      std::cout << GridLogMessage << "dimension " <<dim<<std::endl;
      //      DumpSliceNorm(std::string("Old_exchange from"),in,dim);
      //      DumpSliceNorm(std::string("Old_exchange to  "),padded,dim);

    }
    std::cout << GridLogPerformance << "PaddedCell::Expand timings: cshift:" << tshift/1000 << "ms, insert-slice:" << tins/1000 << "ms" << std::endl;
    
    return padded;
  }

  template<class vobj>
  inline Lattice<vobj> ExpandTest(int dim, const Lattice<vobj> &in, const CshiftImplBase<vobj> &cshift = CshiftImplDefault<vobj>()) const
  {
    Coordinate processors=unpadded_grid->_processors;
    GridBase *old_grid = in.Grid();
    GridCartesian *new_grid = grids[dim];//These are new grids
    Lattice<vobj>  padded(new_grid);
    Lattice<vobj> shifted(old_grid);    
    Coordinate local     =old_grid->LocalDimensions();
    Coordinate plocal    =new_grid->LocalDimensions();
    if(dim==0) conformable(old_grid,unpadded_grid);
    else       conformable(old_grid,grids[dim-1]);

    //    std::cout << " dim "<<dim<<" local "<<local << " padding to "<<plocal<<std::endl;
    double tins=0, tshift=0;

    int islocal = 0 ;
    if ( processors[dim] == 1 ) islocal = 1;

    if ( islocal ) {

      // replace with a copy and maybe grid swizzle
      double t = usecond();
      padded = in;
      tins += usecond() - t;
      
    } else {

      //////////////////////////////////////////////
      // Replace sequence with
      // ---------------------
      // (i) Gather high face(s); start comms
      // (ii) Gather low  face(s); start comms
      // (iii) Copy middle bit with localCopyRegion
      // (iv) Complete high face(s), insert slice(s)
      // (iv) Complete low  face(s), insert slice(s)
      //////////////////////////////////////////////
      Face_exchange(in,padded,dim,depth);
    }
    return padded;
  }
  template<class vobj>
  void Face_exchange(const Lattice<vobj> &from,
		     Lattice<vobj> &to,
		     int dimension,int depth) const
  {
    typedef typename vobj::vector_type vector_type;
    typedef typename vobj::scalar_type scalar_type;
    typedef typename vobj::scalar_object sobj;

    RealD t_gather=0.0;
    RealD t_scatter=0.0;
    RealD t_comms=0.0;
    RealD t_copy=0.0;
    
    //    std::cout << GridLogMessage << "dimension " <<dimension<<std::endl;
    //    DumpSliceNorm(std::string("Face_exchange from"),from,dimension);
    GridBase *grid=from.Grid();
    GridBase *new_grid=to.Grid();

    Coordinate lds = from.Grid()->_ldimensions;
    Coordinate nlds=   to.Grid()->_ldimensions;
    Coordinate simd= from.Grid()->_simd_layout;
    int ld    = lds[dimension];
    int nld   = to.Grid()->_ldimensions[dimension];
    

    assert(depth<=lds[dimension]); // A must be on neighbouring node
    assert(depth>0);   // A caller bug if zero
    assert(ld+2*depth==nld);
    ////////////////////////////////////////////////////////////////////////////
    // Face size and byte calculations
    ////////////////////////////////////////////////////////////////////////////
    int buffer_size = 1;
    for(int d=0;d<lds.size();d++){
      if ( d!= dimension) buffer_size=buffer_size*lds[d];
    }
    int rNsimd = vobj::Nsimd() / simd[dimension];
    assert( buffer_size == from.Grid()->_slice_nblock[dimension]*from.Grid()->_slice_block[dimension] *rNsimd);

    static cshiftVector<sobj> send_buf; 
    static cshiftVector<sobj> recv_buf;
    send_buf.resize(buffer_size*2*depth);    
    recv_buf.resize(buffer_size*2*depth);

    int words = buffer_size;
    int bytes = words * sizeof(sobj);
    ////////////////////////////////////////////////////////////////////////////
    // Gather all surface terms up to depth "d"
    ////////////////////////////////////////////////////////////////////////////
    RealD t=usecond();
    int plane=0;
    for ( int d=0;d < depth ; d ++ ) {
      GatherSlice(send_buf,from,d,dimension,plane*buffer_size); plane++;
    }
    for ( int d=0;d < depth ; d ++ ) {
      GatherSlice(send_buf,from,ld-depth+d,dimension,plane*buffer_size); plane++;
    }
    t_gather= usecond() - t;

    ////////////////////////////////////////////////////////////////////////////
    // Communicate
    ////////////////////////////////////////////////////////////////////////////
    int comm_proc = 1;
    int xmit_to_rank;
    int recv_from_rank;
    grid->ShiftedRanks(dimension,comm_proc,xmit_to_rank,recv_from_rank);

    t=usecond();
    for(int d = 0; d<depth;d++){
      grid->SendToRecvFrom((void *)&send_buf[d*buffer_size], xmit_to_rank,
			   (void *)&recv_buf[(d+depth)*buffer_size], recv_from_rank, bytes);
  
      grid->SendToRecvFrom((void *)&send_buf[(d+depth)*buffer_size], recv_from_rank,
			   (void *)&recv_buf[d*buffer_size], xmit_to_rank, bytes);
    }
    t_comms= usecond() - t;

    ////////////////////////////////////////////////////////////////////////////
    // Copy interior -- overlap this with comms
    ////////////////////////////////////////////////////////////////////////////
    int Nd = new_grid->Nd();
    Coordinate LL(Nd,0);
    Coordinate sz = grid->_ldimensions;
    Coordinate toLL(Nd,0);
    toLL[dimension]=depth;
    t=usecond();
    localCopyRegion(from,to,LL,toLL,sz);
    t_copy= usecond() - t;
    
    ////////////////////////////////////////////////////////////////////////////
    // Scatter all faces
    ////////////////////////////////////////////////////////////////////////////
    //    DumpSliceNorm(std::string("Face_exchange to before scatter"),to,dimension);
    plane=0;
    t=usecond();
    for ( int d=0;d < depth ; d ++ ) {
      ScatterSlice(recv_buf,to,d,dimension,plane*buffer_size); plane++;
    }
    //    DumpSliceNorm(std::string("Face_exchange to scatter 1st "),to,dimension);
    for ( int d=0;d < depth ; d ++ ) {
      ScatterSlice(recv_buf,to,nld-depth+d,dimension,plane*buffer_size); plane++;
    }
    t_scatter= usecond() - t;
    std::cout << GridLogPerformance << "PaddedCell::Expand new timings: gather :" << t_gather/1000  << "ms"<<std::endl;
    std::cout << GridLogPerformance << "PaddedCell::Expand new timings: scatter:" << t_scatter/1000   << "ms"<<std::endl;
    std::cout << GridLogPerformance << "PaddedCell::Expand new timings: gather :" << 2.0*bytes/t_gather << "MB/s"<<std::endl;
    std::cout << GridLogPerformance << "PaddedCell::Expand new timings: scatter:" << 2.0*bytes/t_scatter<< "MB/s"<<std::endl;
    std::cout << GridLogPerformance << "PaddedCell::Expand new timings: copy   :" << t_copy/1000      << "ms"<<std::endl;
    std::cout << GridLogPerformance << "PaddedCell::Expand new timings: comms  :" << t_comms/1000     << "ms"<<std::endl;
    std::cout << GridLogPerformance << "PaddedCell::Expand new timings: comms  :" << (RealD)4.0*bytes/t_comms   << "MB/s"<<std::endl;
    //    DumpSliceNorm(std::string("Face_exchange to done"),to,dimension);
  }
  
};
 

NAMESPACE_END(Grid);

