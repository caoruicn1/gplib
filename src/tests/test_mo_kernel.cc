#include <boost/test/unit_test.hpp>
#include <armadillo>
#include <vector>

#include "gplib/gplib.hpp"

const double eps = 1e-6;
// #define eps 0.001

BOOST_AUTO_TEST_SUITE( mo_kernels )

BOOST_AUTO_TEST_CASE( mo_eval_lmc_kernel ) {
  /**
   * The evaluation of kernel must be a positive, semidefinite matrix.
   * If it is not correct, cholesky decomposition will arise an error.
   * */
  std::vector<arma::mat> X;
  const int noutputs = 4;
  for (int i = 0; i < noutputs; ++i)
    X.push_back(arma::randn(100, 3));

  std::vector<std::shared_ptr<gplib::kernel_class>> latent_functions;
  std::vector<double> ker_par({0.9, 1.2, 0.1});
  for (int i = 0; i < noutputs - 1; ++i) {
    auto kernel = std::make_shared<gplib::kernels::squared_exponential>(ker_par);
    latent_functions.push_back(kernel);
  }
  std::vector<arma::mat> params(latent_functions.size(), arma::eye<arma::mat>(noutputs, noutputs));
  gplib::multioutput_kernels::lmc_kernel K(latent_functions, params);
  arma::mat ans = K.eval(X, X);
  // std::cout << ans.n_cols << " " << ans.n_rows << std::endl;
  // ans.print();
  arma::mat tmp = arma::chol(ans);
  std::cout << "\033[32m\t eval multioutput lmc_kernel passed ... \033[0m\n";
}

BOOST_AUTO_TEST_CASE( mo_lmc_gradient ) {
  std::vector<arma::mat> X;
  const int noutputs = 4;
  for (int i = 0; i < noutputs; ++i)
    X.push_back(arma::randn(100, 3));

  std::vector<std::shared_ptr<gplib::kernel_class>> latent_functions;
  std::vector<double> k_params({0.9, 1.2, 0.1});
  for (int i = 0; i < noutputs - 1; ++i){
    auto kernel = std::make_shared<gplib::kernels::squared_exponential>(k_params);
    latent_functions.push_back(kernel);
  }
  std::vector<arma::mat> params(latent_functions.size(), arma::eye<arma::mat>(noutputs, noutputs));
  gplib::multioutput_kernels::lmc_kernel K(latent_functions, params);
  int param_id = 0;
  arma::mat analitical;
  arma::mat numeric;
  //param_id = (q*d*d) + i * d + j for MO kernel
  //param_id = (Q * d * d) + q * params + i for normal kernel
  for (int k = 0; k < noutputs - 1; ++k){
    for (int i = 0; i < noutputs; ++i){
      for (int j = 0; j < noutputs; ++j){
        param_id = (k * noutputs * noutputs) + i * noutputs + j;
        analitical = K.derivate(param_id, X, X, i, j);
        params = K.get_params();
        params[k](i, j) += eps;
        K.set_params(params);
        numeric = K.eval(X, X);
        params = K.get_params();
        params[k](i, j) -= 2.0 * eps;
        K.set_params(params);
        numeric -= K.eval(X, X);
        numeric = numeric / (2.0 * eps);
        params = K.get_params();
        params[k](i, j) += eps;
        K.set_params(params);
        for (size_t l = 0; l < numeric.n_rows; ++l){
          for( size_t n = 0; n < numeric.n_cols; ++n){
            BOOST_CHECK_CLOSE (numeric (l, n), analitical (l, n), eps);
          }
        }
      }
    }
  }
  std::cout << "\033[32m\t gradient multioutput lmc_kernel passed ... \033[0m\n";
}

BOOST_AUTO_TEST_SUITE_END()
