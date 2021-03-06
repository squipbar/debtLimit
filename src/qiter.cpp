/***********************************************************************************
 * qiter.cpp
 *
 * Computes the matrix of prices consistent with a given debt limit
 *
 * 22mar2017
 * Philip Barrett, DC
 *
 ***********************************************************************************/

#include "qiter.hpp"
#include "surp.hpp"
#include "q.hpp"

// [[Rcpp::export]]
arma::mat d_prime( int i_x, double d, arma::vec d_bar, double qhat, arma::mat Q,
                   arma::vec d_grid, arma::vec G, arma::vec shift, double lambda, arma::vec e_grid,
                   arma::vec coeff, bool tri,
                   arma::mat D_prime_0, bool D_prime_0_flag, int print_level=1,
                   double tol=1e-05, int maxit=20, bool report=false ){
// Computes the vector of fixed points for the continuation debt satisfying, for
// each (x',e):
//      d' = d * ( (1-lambda) +lambda * q(x',d') ) / ( G(x') * qhat ) - s(x',d) - e

  int n = d_bar.n_elem ;
  int m = e_grid.n_elem ;
      // Dimensions
  vec s = v_surp( d * ones(n), coeff, shift, tri) ;
      // Surpluses
  vec qprime = zeros(m) ;
  mat dprime = zeros(m,n) ;
  vec this_dprime = zeros(m) ;
  vec this_dprime_old = zeros(m) ;
      // Initialize containers
  mat Q_t = Q.t() ;
      // Transpose (need for linear interpolation)

  if(!D_prime_0_flag){
    D_prime_0 = ones(m) * conv_to<rowvec>::from( d / ( G * qhat ) - s )  -
                        e_grid * ones(1,n);
  }
      // create initial D_prime if necessary
          // Rcout << "s:\n" << s << std::endl ;
          // Rcout << "D_prime_0:\n" << D_prime_0 << std::endl ;

  double maxdiff = 0 ;
  int worst_state = 0 ;
  int worst_it = 0 ;
  uvec worst_loc = zeros<uvec>(m) ;

  for( int i = 0 ; i < n ; i++ ){
  // Loop over states
    double diff = 2 * tol ;
    int it = 0 ;
    this_dprime_old = D_prime_0.col(i) ;
        // Iteration variables
    if( print_level > 0 ){
      Rcout << "\nState " << i << std::endl ;
    }
    vec q_bar = ones(1) ;
    vec v_dbar = ones(1) * d_bar(i) ;
    interp1( d_grid, Q_t.col(i), v_dbar, q_bar, "linear", 1 ) ;
        // Compute q_bar
    uvec has_sol = e_grid >= d * ( 1 - lambda + lambda * q_bar(0) ) / ( G(i) * qhat ) -
                      s(i) - d_bar(i) ;
    uvec loc_sol = find( has_sol ) ;
        // There is no solution when e is too low to find a solution at the debt
        // limit.
          // Rcout << "has_sol:\n" << has_sol << std::endl ;
          // Rcout << "loc_sol:\n" << loc_sol << std::endl ;

    while( diff > tol && it < maxit && any(has_sol) ){
      it++ ;
      // this_dprime.elem( find( this_dprime > d_bar(i) ) ).fill( d_bar(i) ) ;
          // It the guess is above d_bar, cap it
      interp1( d_grid, Q_t.col(i), this_dprime, qprime, "linear", 1 ) ;
          // Interpolate the price
      this_dprime.elem(loc_sol) =
                 .95 * clamp( d * ( 1 - lambda + lambda * qprime.elem(loc_sol) ) / ( G(i) * qhat ) -
                      s(i) - e_grid(loc_sol), 0, max(d_grid) ) +
                      .05 * this_dprime_old.elem(loc_sol); // d_bar(i) ) ;
          // Fixed point equations.  Add the gain to avoid oscillations
      diff = max( abs( this_dprime.elem(loc_sol) - this_dprime_old.elem(loc_sol) ) ) / .05 ;
          // Measure difference
      if( print_level > 1 ){
        Rcout << "  it = " << it << std::endl ;
        Rcout << "  diff = " << diff << std::endl ;
        // Rcout << "  this_dprime:\n " << this_dprime ;
        // Rcout << "  this_dprime_old:\n" << this_dprime_old ;
        // Rcout << "  qprime:\n" << qprime ;
        // Rcout << "  e_grid:\n" << e_grid ;
      }
      this_dprime_old = this_dprime ;
          // Update iteration
    }
    if( any( 1 - has_sol ) ){
      this_dprime.elem(find( 1 - has_sol ) ).fill( max(d_grid) ) ;
    }else{
      if( diff > maxdiff ){
        maxdiff = diff ;
        worst_state = i ;
        worst_it = it ;
        worst_loc = loc_sol ;
      }
    }
    dprime.col(i) = this_dprime ;
        // Save output
  }

  if( report & (maxdiff > 0) & (maxdiff > tol) ){
  // Report failure
    Rcout << "Maxdiff = " << maxdiff << std::endl ;
    Rcout << "worst_state = " << worst_state << std::endl ;
    Rcout << "worst_it = " << worst_it << std::endl ;
    Rcout << "worst_loc = " << worst_loc << std::endl ;
  }

  return dprime ;
}


// [[Rcpp::export]]
arma::vec q_e( double d, arma::vec d_bar, arma::vec qhat, arma::mat Q,
                   arma::vec d_grid, arma::vec G, arma::vec shift, double lambda, arma::vec e_grid,
                   arma::vec coeff, bool tri, arma::mat D_prime_0, bool D_prime_0_flag,
                   arma::mat trans, int print_level=1, double tol=1e-05, int maxit=20 ){
// Computes the expected continuation price q_e in each state.  Takes as an
// input a vector of guesses qhat.

  int n = d_bar.n_elem ;
  int m = e_grid.n_elem ;
      // Dimensions
  vec out = zeros(n) ;
      // Initialize output
  mat temp_dprime = zeros(m,n) ;
  umat temp_II = zeros<umat>(m,n) ;
  mat temp_II_density = zeros(m,n) ;
      // Temporary containers for dprime and the indicator for conditional default
  vec temp_qprime = zeros(m) ;
  mat m_qprime = zeros(m,n) ;
      // Initialize dprime and qprime
  mat Q_t = Q.t() ;
      // Transpose (need for linear interpolation)

  for( int i = 0 ; i < n ; i++ ){
    temp_dprime = d_prime( i,  d, d_bar, qhat(i), Q, d_grid, G, shift, lambda, e_grid, coeff,
                           tri, D_prime_0, D_prime_0_flag, print_level - 1, tol, maxit ) ;
        // The continuation debt level
    for( int j = 0 ; j < n ; j++ ){
      interp1( d_grid, Q_t.col(j), temp_dprime.col(j), temp_qprime, "linear", 1 ) ;
      m_qprime.col(j) = temp_qprime ;
          // Interpolate and store the price
      temp_II.col(j) = ( temp_dprime.col(j) <= d_bar(j) ) ;
          // Record if there is no conditional default
      temp_II_density.col(j) = conv_to<vec>::from(temp_II.col(j)) ;
      temp_II_density.col(j) = temp_II_density.col(j) *
              ( (sum(temp_II_density.col(j)) > 0) ? (1 / sum(temp_II_density.col(j))) : 0 ) ;
          // Create the density if the repayment probability is strictly positive
    }
    out(i) = dot( trans.row(i), ones<rowvec>(m) * ( m_qprime % temp_II_density ) ) ;
        // Create the expected value conditional on repayment
    if( print_level > 0 ){
      Rcout << "temp_II:\n" << temp_II << std::endl ;
      Rcout << "temp_II_density:\n" << temp_II_density << std::endl ;
    }
    // out(i) = dot( trans.row(i), ones<rowvec>(m) * m_qprime / m ) ;
  }
  return out ;
}

// [[Rcpp::export]]
arma::vec q_hat_fn( double d, arma::vec p, arma::vec d_bar, arma::vec qhat, arma::mat Q,
               arma::vec d_grid, arma::vec R, arma::vec G, arma::vec shift, double lambda, double phi,
               arma::vec e_grid, arma::vec coeff, bool tri, arma::mat D_prime_0,
               bool D_prime_0_flag,arma::mat trans, int print_level=1, double tol=1e-05,
               int maxit=50, double d_tol=1e-05, int d_maxit=20 ){
// Inner iterator to find the vector of q over x given debt d and next-period
// default probability vector p

  int n = p.n_elem ;
      // Number of states
  vec q = zeros(n) ;
      // Iitialize output
  mat def = zeros(1,1) ;
      // Dummy variable
  double diff = 2 * tol ;
  int it = 0 ;
  vec q_old = 2 * ones(n) ;
      // Initialize loop variables
  while( diff > tol && it < maxit ){
    it++ ;
    vec qe = q_e( d, d_bar, qhat, Q, d_grid, G, shift, lambda, e_grid, coeff, tri, D_prime_0,
                  D_prime_0_flag, trans, print_level - 1, d_tol, d_maxit ) ;
        // Compute expected continuation price
    q = q_fn( R, p, trans, lambda, phi, n, "fix", G, qe, def ) ;
        // Use fix because computing qe is the whole point!
    qhat = .95 * q + .05 * qhat ;
        // Gain less than one to hinder oscillation
    diff = max( abs( q - qhat) ) / .05 ;
        // The difference
    if( print_level > 0 ){
      if(print_level > 1){
        Rcout << std::endl ;
      }
      Rcout << "q iteration = " << it << ", diff = " << diff << std::endl ;
    }
  }
  return q ;
}



// [[Rcpp::export]]
arma::mat q_hat_mat( arma::mat P, arma::vec d_bar, arma::mat QHat, arma::mat Q,
                    arma::vec d_grid, arma::vec R, arma::vec G, arma::vec shift, double lambda, double phi, arma::vec e_grid,
                    arma::vec coeff, bool tri, arma::mat D_prime_0, bool D_prime_0_flag,
                    arma::mat trans, int print_level=0, double tol=1e-04, int maxit=50,
                    double d_tol=1e-05, int d_maxit=20 ){
// Computes the matrix of debt prices on the grid consistent with the assumed
// default probabilities.

  mat out = zeros(size(Q)) ;
      // Initialize output
  int m = d_grid.n_elem ;
      // Number of debt levels
  for( int i = 0 ; i < m ; i++ ){
    if( print_level > 0 ){
      Rcout << "\nDebt grid point # " << i << std::endl ;
    }
    out.col(i) =  q_hat_fn( d_grid(i), P.col(i), d_bar, QHat.col(i), Q, d_grid, R,
            G, shift, lambda, phi, e_grid, coeff, tri, D_prime_0, D_prime_0_flag, trans,
            print_level - 1, tol, maxit, d_tol, d_maxit ) ;
  }
  return out ;
}


// [[Rcpp::export]]
arma::mat d_prime_mat( arma::vec d_bar, arma::mat QHat, arma::mat Q,
                       arma::vec d_grid, arma::vec G, arma::vec shift, double lambda, arma::vec e_grid,
                       arma::mat trans, arma::vec coeff, bool tri,
                       arma::mat D_prime_0, bool D_prime_0_flag, int print_level=1,
                       double tol=1e-05, int maxit=20 ){
// Computes a matrix of *average* continuation debt levels.

  mat out = zeros(size(Q)) ;
      // Initialize output
  int m = d_grid.n_elem ;
  int n = d_bar.n_elem ;
      // Number of states and debt levels
  for( int i=0 ; i < n ; i++ ){
    for( int j=0 ; j < m ; j++ ){
      out(i,j) = dot( trans.row(i),
          mean( d_prime( i, d_grid(j), d_bar, QHat(i,j), Q, d_grid, G, shift, lambda, e_grid,
                      coeff, tri, D_prime_0, D_prime_0_flag, print_level-1, tol, maxit ),
                      0 ) ) ;
            // mean(.,0) takes the column mean (i.e. the mean across e_grid with
            // equiprobable nodes), and then do sumprod with appropriate
            // transition probabilities
    }
  }
  return out ;
}

// [[Rcpp::export]]
arma::mat qe_mat( arma::vec d_bar, arma::mat QHat, arma::mat Q,
                       arma::vec d_grid, arma::vec G, arma::vec shift, double lambda, arma::vec e_grid,
                       arma::mat trans, arma::vec coeff, bool tri,
                       arma::mat D_prime_0, bool D_prime_0_flag, int print_level=0,
                       double tol=1e-05, int maxit=20 ){
// Creates a matrix of expected continuation debt values

  mat out = zeros(size(Q)) ;
      // Initialize output
  int m = d_grid.n_elem ;
      // Number of debt levels
  for( int i = 0 ; i < m ; i++ ){
    if( print_level > 0 ){
      Rcout << "\nDebt grid point # " << i << std::endl ;
    }
    out.col(i) =  q_e( d_grid(i), d_bar, QHat.col(i), Q, d_grid, G, shift,
            lambda, e_grid, coeff, tri, D_prime_0, D_prime_0_flag, trans,
            print_level - 1, tol, maxit ) ;
  }
  return out ;
}

// arma::vec q_e( double d, arma::vec d_bar, arma::vec qhat, arma::mat Q,
//                arma::vec d_grid, arma::vec G, double lambda, arma::vec e_grid,
//                arma::vec coeff, bool tri, arma::mat D_prime_0, bool D_prime_0_flag,
//                arma::mat trans, int print_level, double tol, int maxit )








