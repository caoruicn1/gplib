#include "gplib.hpp"
#include <nlopt.hpp>

using namespace arma;
using namespace std;

namespace gplib {

  struct gp_reg_multi::implementation {
    shared_ptr<multioutput_kernel_class> kernel;
    vector<mat> X;
    vector<vec> y;
    vector<mat> M;
    double sigma = 0.01;
    size_t state;

    vec eval_mean(vector<mat> &data) {
      size_t total_size = 0;
      for (size_t i = 0; i < data.size(); ++i) {
        total_size += data[i].n_rows;
      }
      return zeros<vec> (total_size);
    }

    mv_gauss predict(const vector<mat> &new_data) {
      //Add new data to observations
      vector<mat> M(X.size());
      vec fill_y;
      size_t total_rows = 0;
      for (size_t i = 0; i < X.size(); i++) {
        M[i] = join_vert (X[i], new_data[i]);
        fill_y = join_cols<mat> (fill_y, y[i]);
        fill_y = join_cols<mat> (fill_y, zeros<vec>(new_data[i].n_rows));
        total_rows += M[i].n_rows;
      }

      //Compute Covariance
      mat cov = kernel -> eval(M, M);
      //Set mean
      vec mean = eval_mean(M);
      //Set alredy observed Values
      vector<bool> observed(mean.n_rows, false);
      size_t start = 0;
      for (size_t i = 0; i < M.size(); i++) {
        for (size_t j = 0; j < X[i].n_rows; j++)
          observed[start + j] = true;
        start += M[i].n_rows;
      }
      //Conditon Multivariate Gaussian
      mv_gauss gd(mean, cov);
      return gd.conditional(fill_y, observed);
    }

    mat comp_Q(const vector<mat> &a, const vector<mat> &b, vector<mat> &u) {
      mat kuu =  kernel-> eval(u, u).i();
      return kernel-> eval(a, u) * kuu * kernel-> eval(u, b);
    }

    mv_gauss predict_FITC(const vector<mat> &new_x) {
      mat Qn = comp_Q(X, X, M);
      mat Qm = comp_Q(new_x, new_x, M);
      mat lambda = diagmat(kernel-> eval(X, X) - Qn);
      lambda = (lambda + sigma * eye(lambda.n_rows, lambda.n_cols)).i();
      mat E = (kernel-> eval(M, M) + kernel-> eval(M, X) * lambda *
              kernel-> eval(X, M)).i();
      mat Y = flatten(y);
      mat mean = kernel-> eval(new_x, M) * E * kernel-> eval(M, X) *
                 lambda * Y;
      mat cov = kernel-> eval(new_x, new_x) - Qm + kernel-> eval(new_x, M)
                * E * kernel-> eval(M, new_x);

      cout << "antes de" << endl;
      return mv_gauss(mean, cov + 1e-9 * eye<mat>(cov.n_rows, cov.n_cols));
    }

    mv_gauss marginal() {
      vec mean = eval_mean(X);
      mat cov = kernel-> eval(X, X);
      return mv_gauss(mean, cov);
    }

    mat flatten(vector<vec> &y) {
      mat flat;
      for (size_t i = 0; i < y.size(); i++) {
        flat = join_cols<mat> (flat, y[i]);
      }
      return flat;
    }

    vector<double> flatten(vector<mat> &M) {
      size_t t_size = 0;
      for (size_t i = 0; i < M.size(); ++i)
        t_size += M[i].size();

      vector<double> ans(t_size);
      size_t iter = 0;
      for (size_t q = 0; q < M.size(); ++q) {
        copy (M[q].begin(), M[q].end(), ans.begin() + iter);
        iter += M[q].size();
      }
      return ans;
    }

    // TODO: don't set values in this function. It must return the matrix.
    void unflatten(vector<double> &M_params) {
      size_t iter = 0;
      for(size_t q = 0; q < M.size(); ++q)
        for(size_t i = 0; i < M[q].n_rows; ++i)
          for(size_t j = 0; j < M[q].n_cols; ++j) {
            M[q](i, j) = M_params[iter];
            ++iter;
          }
    }

    void split(const vector<double> &theta, vector<double> &kernel_params,
              vector<double> &M_params) {
      copy(theta.begin(), theta.begin() + kernel_params.size(),
          kernel_params.begin());

      copy(theta.begin() + kernel_params.size() + 1, theta.end(),
           M_params.begin());
    }

    void set_params(const vector<double> &params) {
      // TODO: set sigma.
      size_t M_size = 0;
      for(size_t i = 0; i < M.size(); ++i) {
        M_size += M[i].size();
      }
      vector<double> kernel_params(params.size() - M_size), M_params(M_size);
      split(params, kernel_params, M_params);
      kernel-> set_params(kernel_params);
      unflatten(M_params);
    }

    vector<double> get_params() {
      // TODO: get sigma.
      vector<double> params = kernel-> get_params();
      vector<double> flatten_M = flatten(M);
      params.insert(params.end(), flatten_M.begin(), flatten_M.end());
      return params;
    }

    double log_marginal() {
      return marginal().log_density(flatten(y));
    }

    double log_marginal_fitc() {
      size_t N = 0;
      for (size_t i = 0; i < X.size(); ++i)
        N += X[i].n_rows;

      mat Qff = comp_Q (X, X, M);
      mat lambda = diagmat( kernel-> eval (X, X) - Qff) +
        // TODO: don't set values in this function. It must return the matrix.
                  sigma * eye<mat> (Qff.n_rows, Qff.n_cols);
      mat B = chol(Qff + lambda);
      double log_det = 0;
      for (size_t i = 0; i < Qff.n_rows; ++i)
        log_det += log(B(i, i));
      double ans = -log_det;
      mat flat_y = flatten (y);
      mat tmp = (flat_y.t() * (Qff + lambda).i() * flat_y);
      ans -= 0.5 * tmp(0,0);
      ans -= 0.5 * N * log (2.0 * pi);
      return ans;
    }

    static double training_obj(const vector<double> &theta,
        vector<double> &grad, void *fdata) {

      implementation *pimpl = (implementation*) fdata;
      pimpl-> kernel-> set_params(theta);
      double ans = pimpl-> log_marginal();

      vec mx = pimpl-> eval_mean(pimpl-> X);
      mat K = pimpl-> kernel-> eval(pimpl-> X, pimpl-> X);
      mat Kinv = K.i();
      vec diff = pimpl-> flatten(pimpl-> y);
      mat dLLdK = -0.5 * Kinv + 0.5 * Kinv * diff * diff.t() * Kinv;
      for (size_t d = 0; d < grad.size(); d++) {
        mat dKdT = pimpl-> kernel-> derivate(d, pimpl-> X, pimpl-> X);
        grad[d] = trace(dLLdK * dKdT);
      }
      cout << "ANS: " << ans << endl;
      return ans;
    }

    static double training_obj_FITC(const vector<double> &theta,
        vector<double> &grad, void *fdata) {

      implementation *pimpl = (implementation*) fdata;
      // TODO: (We need to set sigma there too).
      pimpl-> set_params(theta);

      double ans = pimpl-> log_marginal_fitc();

#if 0
      mat flat_y = pimpl-> flatten (pimpl-> y);
      mat Qff = pimpl-> comp_Q (pimpl-> X, pimpl-> X, pimpl-> M);
      mat lambda = diagmat (pimpl-> kernel-> eval (pimpl-> X, pimpl-> X) -
                  Qff) + pimpl-> sigma * eye<mat> (Qff.n_rows, Qff.n_cols);
      mat Ri = (Qff + lambda).i();
      mat ytRi = flat_y.t() * Ri;
      mat Riy = Ri * flat_y;
      mat Kuui = (pimpl-> kernel-> eval (pimpl-> M, pimpl-> M)).i();
      mat Kuf = pimpl-> kernel-> eval(pimpl-> M, pimpl-> X);
      mat KuuiKuf = Kuui * Kuf;
      mat Kfu = pimpl-> kernel-> eval(pimpl-> X, pimpl-> M);
      mat KfuKuui = Kfu * Kuui;

      // TODO: write special case for T = sigma
      for (size_t d = 0; d < grad.size(); d++) {
        mat dKfudT = pimpl-> kernel-> derivate (d, pimpl-> X, pimpl-> M);
        mat dKuudT = pimpl-> kernel-> derivate (d, pimpl-> M, pimpl-> M);
        mat dKufdT = pimpl-> kernel-> derivate (d, pimpl-> M, pimpl-> X);
        mat dKffdT = pimpl-> kernel-> derivate (d, pimpl-> X, pimpl-> X);
        // mat dQffdT = KfuKuui * (dKufdT - dKuudT * KuuiKuf) + dKfudT * KuuiKuf;
        mat dQffdT = Kfu * (Kuui * dKufdT  - (Kuui * dKuudT * Kuui) * Kuf) +
          dKfudT * Kuui * Kuf;
        mat dRdT = dQffdT + diagmat(dKffdT) - diagmat(dQffdT);
        mat ans  = -trace(Ri * dRdT) + ytRi * dRdT * Riy;
        grad[d]  = 0.5 * ans(0,0);
      }

#else
      vector<double> grad2(grad.size());
      vector<double> params = theta;
      vector<double> lb = pimpl-> kernel-> get_lower_bounds();
      vector<double> ub = pimpl-> kernel-> get_upper_bounds();
      double eps = 1e-6;
      for (size_t d = 0; d < grad.size(); d++) {
        if ((d < lb.size() && ub[d] > lb[d]) || d >= lb.size()) {
          params[d] += eps;
          pimpl-> set_params(params);
          double cur = pimpl-> log_marginal_fitc();
          params[d] -= 2 * eps;
          pimpl-> set_params(params);
          cur -= pimpl-> log_marginal_fitc();
          params[d] += eps;
          pimpl-> set_params(params);
          cur /= 2.0 * eps;
          grad2[d] = cur;
        } else {
          grad2[d] = 0;
        }

        /*if (fabs(grad2[d] - grad[d]) > 1e-5) {
          cout << "Difference found at : " << d << endl;
          cout << "\t" << grad[d] << " : " << grad2[d] << endl;
        }*/
        grad[d] = grad2[d];
      }
#endif
      std::cout << "ANS: " << ans << std::endl;
      return ans;
    }


    double train(int max_iter) {
      nlopt::opt best(nlopt::LD_MMA, kernel-> n_params());
      best.set_max_objective(implementation::training_obj, this);
      best.set_xtol_rel(1e-4);
      best.set_maxeval(max_iter);

      best.set_lower_bounds(kernel-> get_lower_bounds());
      best.set_upper_bounds(kernel-> get_upper_bounds());

      double error; //final value of error function
      vector<double> x = kernel-> get_params();
      best.optimize(x, error);
      kernel-> set_params(x);
      return error;
    }

    double train_FITC(int max_iter) {
      // TODO: receive tol as parameter.
      size_t M_size = M.size() * M[0].size(); // TODO: Compute M_size using all the matrices in M
      size_t n_params = kernel-> n_params() + M_size;
      nlopt::opt best(nlopt::LD_MMA, n_params);
      best.set_max_objective(implementation::training_obj_FITC, this);
      best.set_xtol_rel(1e-4);
      best.set_maxeval(max_iter);
      vector<double> lb = kernel-> get_lower_bounds();
      vector<double> ub = kernel-> get_upper_bounds();
      lb.resize(lb.size() + M_size, -HUGE_VAL);
      ub.resize(ub.size() + M_size, HUGE_VAL);
      // assert(lb.size() == n_params);
      best.set_lower_bounds(lb);
      best.set_upper_bounds(ub);



      double error; //final value of error function
      vector<double> x = get_params();
      cout << "before optimization" << endl;
      best.optimize(x, error);
      cout << "after optimization" << endl;
      set_params(x);

      return error;
    }

  };

  gp_reg_multi::gp_reg_multi() {
    pimpl = new implementation();
  }

  gp_reg_multi::~gp_reg_multi() {
    delete pimpl;
  }

  void gp_reg_multi::set_kernel(const shared_ptr<multioutput_kernel_class> &k) {
    pimpl-> kernel = k;
  }

  void gp_reg_multi::set_training_set(const vector<mat> &X,
      const vector<vec> &y) {

    pimpl-> X = X;
    pimpl-> y = y;
  }

  double gp_reg_multi::train(const int max_iter, const size_t type,
      void *param) {

    // TODO: add option to set different num_pi for each output.
    pimpl-> state = type;
    if (type == FITC) {
      size_t num_pi = *((size_t *) param);
      pimpl-> M = vector<mat> (pimpl-> X.size(),
                  randi<mat>(num_pi, pimpl-> X[0].n_cols, distr_param(0, +50)));

      return pimpl-> train_FITC(max_iter);
    } else
      return pimpl-> train(max_iter);
  }

  mv_gauss gp_reg_multi::full_predict(const vector<mat> &new_data) {
    if (pimpl-> state == FITC)
      return pimpl-> predict_FITC(new_data);
    else
      return pimpl-> predict(new_data);
  }

  arma::vec gp_reg_multi::predict(const vector<arma::mat> &new_data) const {
    mv_gauss g;
    if (pimpl-> state == FITC)
      g = pimpl-> predict_FITC(new_data);
    else
      g = pimpl-> predict(new_data);
    return g.get_mean();
  }
};
