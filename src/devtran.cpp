// Copyright (C) 2013 - 2017  Metrum Research Group, LLC
//
// This file is part of mrgsolve.
//
// mrgsolve is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// mrgsolve is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with mrgsolve.  If not, see <http://www.gnu.org/licenses/>.


/**
 * @mainpage
 * 
 * Documentation for `mrgsolve` `C++` code.
 *
 * To see functions available in the `mrgx` plugin, see the 
 * `mrgx` module <a href="group__mrgx.html">here</a>.
 *
 *
 */

/**
 * @file devtran.cpp
 * 
 */



#include <boost/shared_ptr.hpp>
#include <boost/pointer_cast.hpp>
#include <string>
#include "mrgsolve.h"
#include "odeproblem.h"
#include "dataobject.h"
#include "RcppInclude.h"


#define CRUMP(a) throw Rcpp::exception(a,false)
#define REP(a)   Rcpp::Rcout << #a << std::endl;
#define nREP(a)  Rcpp::Rcout << a << std::endl;
#define say(a)   Rcpp::Rcout << a << std::endl;
#define __ALAG_POS -1200



/** Perform a simulation run.
 * 
 * @param parin list of data and options for the simulation
 * @param inpar numeric parameter values
 * @param parnames parameter names
 * @param init numeric initial values
 * @param cmtnames compartment names
 * @param capture indices in capture vector to actually get
 * @param funs list of pointer addresses to model functions generated by 
 * getNativeSymbolInfo()
 * @param data the main data set
 * @param idata the idata data aset
 * @param OMEGA between-ID normal random effects
 * @param SIGMA within-ID normal random effects
 * @return list containing matrix of simulated data and a character vector of
 * tran names that may have been carried into the output
 *
 */
// [[Rcpp::export]]
Rcpp::List DEVTRAN(const Rcpp::List parin,
                   const Rcpp::NumericVector& inpar,
                   const Rcpp::CharacterVector& parnames,
                   const Rcpp::NumericVector& init,
                   Rcpp::CharacterVector& cmtnames,
                   const Rcpp::IntegerVector& capture,
                   const Rcpp::List& funs,
                   const Rcpp::NumericMatrix& data,
                   const Rcpp::NumericMatrix& idata,
                   Rcpp::NumericMatrix& OMEGA,
                   Rcpp::NumericMatrix& SIGMA,
                   Rcpp::Environment envir) {
  
  const unsigned int verbose  = Rcpp::as<int>    (parin["verbose"]);
  const bool debug            = Rcpp::as<bool>   (parin["debug"]);
  const int digits            = Rcpp::as<int>    (parin["digits"]);
  const double tscale         = Rcpp::as<double> (parin["tscale"]);
  const bool obsonly          = Rcpp::as<bool>   (parin["obsonly"]);
  bool obsaug                 = Rcpp::as<bool>   (parin["obsaug"] );
  obsaug = obsaug & (data.nrow() > 0);
  const int  recsort          = Rcpp::as<int>    (parin["recsort"]);
  const bool filbak           = Rcpp::as<bool>   (parin["filbak"]);
  const double mindt          = Rcpp::as<double> (parin["mindt"]);
  const bool tad              = Rcpp::as<bool>   (parin["tad"]);
  
  // Create data objects from data and idata
  dataobject dat(data,parnames);
  dat.map_uid();
  dat.locate_tran();
  
  dataobject idat(idata, parnames, cmtnames);
  idat.idata_row();
  
  // Number of individuals in the data set
  const int NID = dat.nid();
  const int nidata = idat.nrow();
  
  int j = 0;
  unsigned int k = 0;
  unsigned int crow = 0; 
  size_t h = 0;
  
  bool put_ev_first = false;
  bool addl_ev_first = true;
  
  switch (recsort) {
  case 1:
    break;
  case 2:
    put_ev_first = false;
    addl_ev_first = false;
    break;
  case 3:
    put_ev_first = true;
    addl_ev_first = true;
    break;
  case 4:
    put_ev_first = true;
    addl_ev_first = false;
    break;
  default:
    CRUMP("recsort must be 1, 2, 3, or 4.");
  }
  
  // Requested compartments  
  Rcpp::IntegerVector request = parin["request"];
  const unsigned int nreq = request.size();
  
  // Columns from the data set to carry:
  Rcpp::CharacterVector data_carry_ = Rcpp::as<Rcpp::CharacterVector >(parin["carry_data"]);
  const Rcpp::IntegerVector data_carry =  dat.get_col_n(data_carry_);
  const unsigned int n_data_carry = data_carry.size();
  
  // Columns from the idata set to carry:
  unsigned int n_idata_carry=0;
  Rcpp::IntegerVector idata_carry;  
  if(nidata > 0) {
    Rcpp::CharacterVector idata_carry_ = Rcpp::as<Rcpp::CharacterVector >(parin["carry_idata"]);
    idata_carry =  idat.get_col_n(idata_carry_);
    n_idata_carry = idata_carry.size();
    dat.check_idcol(idat);
  }
  
  // Tran Items to carry:
  Rcpp::CharacterVector tran_carry = Rcpp::as<Rcpp::CharacterVector >(parin["carry_tran"]);
  const unsigned int n_tran_carry = tran_carry.size();
  
  // Captures
  const unsigned int n_capture  = capture.size()-1;
  
  // Create odeproblem object  
  odeproblem *prob  = new odeproblem(inpar, init, funs, capture.at(0));
  prob->omega(OMEGA);
  prob->sigma(SIGMA);
  prob->copy_parin(parin);
  prob->pass_envir(&envir);
  const unsigned int neq = prob->neq();
  
  recstack a(NID);
  
  unsigned int obscount = 0;
  unsigned int evcount = 0;
  dat.get_records(a, NID, neq, obscount, evcount, obsonly, debug);
  
  // Find tofd 
  std::vector<double> tofd;
  if(tad) {
    tofd.reserve(a.size());
    for(recstack::const_iterator it = a.begin(); it !=a.end(); ++it) {
      for(reclist::const_iterator itt = it->begin(); itt != it->end(); ++itt) {
        if((*itt)->evid()==1) {
          tofd.push_back((*itt)->time());
          break;
        }
      }
    }
    if(tofd.size()==0) {
      tofd.resize(a.size(),0.0); 
    }
    if(tofd.size() != a.size()) {
      CRUMP("There was a problem finding time of first dose.");
    }
  }
  
  dvec mtimes = Rcpp::as<dvec>(parin["mtime"]);
  
  // Need this for later
  int nextpos = put_ev_first ?  (data.nrow() + 10) : -100;
  
  if((obscount == 0) || (obsaug)) {
    
    dvec ptimes = Rcpp::as<dvec>(parin["ptimes"]);
    
    Rcpp::NumericMatrix tgrid = Rcpp::as<Rcpp::NumericMatrix>(parin["tgridmatrix"]);
    
    // Already has C indexing
    Rcpp::IntegerVector tgridi = Rcpp::as<Rcpp::IntegerVector>(parin["whichtg"]);
    
    if(tgridi.size() == 0) tgridi = Rcpp::rep(0,NID);
    
    if(tgridi.size() < NID) CRUMP("Length of design indicator less than NID.");
    
    if(max(tgridi) >= tgrid.ncol()) {
      CRUMP("Insufficient number of designs specified for this problem.");
    }
    
    // Number of non-na times in each design
    std::vector<int> tgridn;
    if(tgrid.ncol() > 1) {
      for(int i = 0; i < tgrid.ncol(); ++i) {
        tgridn.push_back(Rcpp::sum(!Rcpp::is_na(tgrid(Rcpp::_,i))));
      }
    } else {
      tgridn.push_back(tgrid.nrow());
    }
    
    // Create a common dictionary of observation events
    // Vector of vectors
    // Outer vector: length = number of designs
    // Inner vector: length = number of times in that design
    std::vector<std::vector<rec_ptr> > designs;
    
    designs.reserve(tgridn.size());
    
    for(size_t i = 0; i < tgridn.size(); ++i) {
      
      std::vector<rec_ptr> z;
      
      z.reserve(tgridn[i]);
      
      for(int j = 0; j < tgridn[i]; ++j) { 
        rec_ptr obs = NEWREC(tgrid(j,i),nextpos,true);
        z.push_back(obs); 
      }
      designs.push_back(z);
    }
    
    double id;
    size_t n;
    size_t m = ptimes.size();
    
    for(recstack::iterator it = a.begin(); it != a.end(); ++it) {
      
      id = dat.get_uid(it-a.begin());
      
      j = idat.get_idata_row(id);
      
      n = tgridn[tgridi[j]];
      
      it->reserve((it->size() + n + m + 10));
      
      for(h=0; h < n; h++) {
        //it->push_back(designs.at(tgridi[j]).at(h));
        it->push_back(designs[tgridi[j]][h]);
        ++obscount;
      } 
      
      for(h=0; h < m; h++) {
        rec_ptr obs = NEWREC(ptimes[h],nextpos,false);
        it->push_back(obs);
      }
      
      std::sort(it->begin(), it->end(), CompRec());
    }
  }
  
  
  // Create results matrix:
  //  rows: ntime*nset
  //  cols: rep, time, eq[0], eq[1], ..., yout[0], yout[1],...
  const unsigned int NN = obsonly ? obscount : (obscount + evcount);
  int precol = 2 + int(tad);
  const unsigned int n_out_col  = precol + n_tran_carry + n_data_carry + n_idata_carry + nreq + n_capture;
  Rcpp::NumericMatrix ans(NN,n_out_col);
  const unsigned int tran_carry_start = precol;
  const unsigned int data_carry_start = tran_carry_start + n_tran_carry;
  const unsigned int idata_carry_start = data_carry_start + n_data_carry;
  const unsigned int req_start = idata_carry_start+n_idata_carry;
  const unsigned int capture_start = req_start+nreq;
  
  const unsigned int neta = OMEGA.nrow();
  arma::mat eta;
  if(neta > 0) {
    eta = prob->mv_omega(NID);
    prob->neta(neta);
  }
  
  const unsigned int neps = SIGMA.nrow();
  arma::mat eps;
  if(neps > 0) {
    eps = prob->mv_sigma(NN);
    prob->neps(neps);
  }
  
  Rcpp::CharacterVector tran_names;
  if(n_tran_carry > 0) {
    
    Rcpp::CharacterVector::iterator tcbeg  = tran_carry.begin();
    Rcpp::CharacterVector::iterator tcend  = tran_carry.end();
    
    const bool carry_evid = std::find(tcbeg,tcend, "evid")  != tcend;
    const bool carry_cmt =  std::find(tcbeg,tcend, "cmt")   != tcend;
    const bool carry_amt =  std::find(tcbeg,tcend, "amt")   != tcend;
    const bool carry_ii =   std::find(tcbeg,tcend, "ii")    != tcend;
    const bool carry_addl = std::find(tcbeg,tcend, "addl")  != tcend;
    const bool carry_ss =   std::find(tcbeg,tcend, "ss")    != tcend;
    const bool carry_rate = std::find(tcbeg,tcend, "rate")  != tcend;
    const bool carry_aug  = std::find(tcbeg,tcend, "a.u.g") != tcend;
    
    if(carry_evid) tran_names.push_back("evid");
    if(carry_amt)  tran_names.push_back("amt");
    if(carry_cmt)  tran_names.push_back("cmt");
    if(carry_ss)   tran_names.push_back("ss");
    if(carry_ii)   tran_names.push_back("ii");
    if(carry_addl) tran_names.push_back("addl");
    if(carry_rate) tran_names.push_back("rate");
    if(carry_aug)  tran_names.push_back("a.u.g");
    
    
    crow = 0;
    int n = 0;
    for(recstack::const_iterator it = a.begin(); it !=a.end(); ++it) {
      for(reclist::const_iterator itt = it->begin(); itt != it->end(); ++itt) {
        if(!(*itt)->output()) continue;
        n = 0;
        if(carry_evid) {ans(crow,n+precol) = (*itt)->evid();                     ++n;}
        if(carry_amt)  {ans(crow,n+precol) = (*itt)->amt();                      ++n;}
        if(carry_cmt)  {ans(crow,n+precol) = (*itt)->cmt();                      ++n;}
        if(carry_ss)   {ans(crow,n+precol) = (*itt)->ss();                       ++n;}
        if(carry_ii)   {ans(crow,n+precol) = (*itt)->ii();                       ++n;}
        if(carry_addl) {ans(crow,n+precol) = (*itt)->addl();                     ++n;}
        if(carry_rate) {ans(crow,n+precol) = (*itt)->rate();                     ++n;}
        if(carry_aug)  {ans(crow,n+precol) = ((*itt)->pos()==nextpos) && obsaug; ++n;}
        ++crow;
      }
    }
  }
  
  if(((n_idata_carry > 0) || (n_data_carry > 0)) ) {
    dat.carry_out(a,ans,idat,data_carry,data_carry_start,
                  idata_carry,idata_carry_start);
  }
  
  if(verbose||debug)  Rcpp::Rcout << "Solving ... ";
  
  double tto, tfrom;
  crow = 0;
  int this_cmtn = 0;
  double dt = 0;
  double id = 0;
  double maxtime = 0;
  double Fn = 1.0;
  int this_idata_row = 0;
  double told = -1;
  
  prob->nid(dat.nid());
  prob->nrow(NN);
  prob->idn(0);
  prob->rown(0);
  
  prob->config_call();
  
  // i is indexing the subject, j is the record
  
  // LOOP ACROSS IDS:
  // tgrid observations have generic ID
  // We must first figure out the ID we are working with
  // and assign in the object
  for(size_t i=0; i < a.size(); ++i) {
    
    told = -1;
    
    prob->idn(i);
    
    tfrom = a[i].front()->time();
    maxtime = a[i].back()->time();
    
    id = dat.get_uid(i);
    
    this_idata_row  = idat.get_idata_row(id);
    
    prob->reset_newid(id);
    
    if(i==0) {
      prob->newind(0);
    }
    
    for(k=0; k < neta; ++k) prob->eta(k,eta(i,k));
    for(k=0; k < neps; ++k) prob->eps(k,eps(crow,k));

    idat.copy_parameters(this_idata_row,prob);
    
    if(a[i][0]->from_data()) {
      dat.copy_parameters(a[i][0]->pos(), prob);
    } else {
      if(filbak) {
        dat.copy_parameters(dat.start(i),prob);
      }
    }
    
    prob->y_init(init);
    
    idat.copy_inits(this_idata_row,prob);
    
    prob->init_call(tfrom);
    
    if(mtimes.size() > 0) {
      add_mtime(a[i], mtimes, prob->mtime(),(debug||verbose));
    }
    
    for(size_t j=0; j < a[i].size(); ++j) {
      
      if(crow == NN) continue;
      
      prob->rown(crow);
      
      rec_ptr this_rec = a[i][j];
      
      this_rec->id(id);
      
      if(prob->systemoff()) {
        unsigned short int status = prob->systemoff();
        if(status==9) CRUMP("The problem was stopped at user request.");
        if(this_rec->output()) {
          if(status==1) {
            ans(crow,0) = this_rec->id();
            ans(crow,1) = this_rec->time();
            for(k=0; k < n_capture; ++k) {
              ans(crow,(k+capture_start)) = prob->capture(capture[k+1]);
            }
            for(k=0; k < nreq; ++k) {
              ans(crow,(k+req_start)) = prob->y(request[k]);
            }
          } else {
            for(k=0; k < ans.ncol(); ++k) {
              ans(crow,k) = NA_REAL;
            }
          }
          ++crow;
        }
        continue;
      } 
      
      if(this_rec->from_data()) {
        dat.copy_parameters(this_rec->pos(), prob);
      }
      
      tto = this_rec->time();
      dt  = (tto-tfrom)/(tfrom == 0.0 ? 1.0 : tfrom);
      
      if((dt > 0.0) && (dt < mindt)) { 
        tto = tfrom;
      }
      
      if(tad) {
        if((this_rec->evid()==1) && (this_rec->pos() != __ALAG_POS)) {
          told = tto;
        }
      }
      
      if(tto > tfrom) {
        for(k = 0; k < neps; ++k) {
          prob->eps(k,eps(crow,k));
        }
      }
      
      prob->set_d(this_rec);
      
      if(j != 0) {
        prob->newind(2);
        prob->init_call_record(tto);
      }  
      
      // Some non-observation event happening
      if(this_rec->is_dose()) {
        
        this_cmtn = this_rec->cmtn();
        
        Fn = prob->fbio(this_cmtn);
        if(Fn < 0) {
          CRUMP("mrgsolve: bioavailability fraction is less than zero.");
        }
        
        bool sort_recs = false;
        unsigned int sort_offset = 0;
      
        if(this_rec->from_data()) {
          if(this_rec->rate() < 0) {
            prob->rate_main(this_rec);
          }
          if(prob->alag(this_cmtn) > mindt) {
            if(this_rec->ss() > 0) {
              CRUMP("ss dosing records with lag time are not currently supported");
            }
            rec_ptr newev = NEWREC(*this_rec);
            newev->pos(__ALAG_POS);
            newev->phantom_rec();
            newev->time(this_rec->time() + prob->alag(this_cmtn));
            this_rec->unarm();
            reclist::iterator it = a[i].begin()+j;
            advance(it,1);
            a[i].insert(it,newev);
            newev->schedule(a[i], maxtime, addl_ev_first, Fn);
            sort_recs = true;
            sort_offset = 0;
          } else {
            this_rec->schedule(a[i], maxtime, addl_ev_first, Fn); 
            sort_recs = this_rec->needs_sorting();
            sort_offset = 1;
          }
        }
        
        if(this_rec->int_infusion()) {
          rec_ptr evoff = NEWREC(this_rec->cmt(), 
                                 9, 
                                 this_rec->amt(), 
                                 this_rec->time() + this_rec->dur(Fn),
                                 this_rec->rate(), 
                                 -300, 
                                 this_rec->id());
          a[i].push_back(evoff);
          sort_recs = true;
        }
        
        // SORT
        if(sort_recs) {
          std::sort(a[i].begin()+sort_offset,a[i].end(),CompRec()); 
        }
      }
      
      prob->advance(tfrom,tto);
      
      if(this_rec->evid() != 2) {
        this_rec->implement(prob);
      }
      
      prob->table_call();
      
      if(this_rec->output()) {
        ans(crow,0) = this_rec->id();
        ans(crow,1) = this_rec->time();
        if(tad) {
          ans(crow,2) = (told > -1) ? (tto - told) : tto - tofd.at(i);
        }
        k = 0;
        for(unsigned int i=0; i < n_capture; ++i) {
          ans(crow,k+capture_start) = prob->capture(capture[i+1]);
          ++k;
        }
        for(k=0; k < nreq; ++k) {
          ans(crow,(k+req_start)) = prob->y(request[k]);
        }
        ++crow; 
      } 
      if(this_rec->evid()==2) {
        this_rec->implement(prob);
      }
      tfrom = tto;
    }
  }
  if(digits > 0) {
    for(int i=req_start; i < ans.ncol(); ++i) {
      ans(Rcpp::_, i) = signif(ans(Rcpp::_,i), digits);
    }
  }
  if((tscale != 1) && (tscale >= 0)) {
    ans(Rcpp::_,1) = ans(Rcpp::_,1) * tscale;
  }
  delete prob;
  return Rcpp::List::create(Rcpp::Named("data") = ans,
                            Rcpp::Named("trannames") = tran_names);
}


