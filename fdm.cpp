#include "fdm.hpp"
#include "pde_boundary_conditions.hpp"
#include "interface.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <cmath>
#include "math.h"
#include <numeric>
#include <functional>

namespace dauphine
{

	fdm_interface::fdm_interface(pde* pde, payoff* pay, rate* r, double f0, double fN, int dt, int dx, double theta)
		: m_pde(pde), m_payoff(pay), m_r(r), m_f0(f0), m_fN(fN), m_dt(dt), m_dx(dx), m_theta(theta)
	{
	}

	fdm_interface::~fdm_interface()
	{
		delete m_pde; //destruction du pointeur
		delete m_r;
		delete m_payoff;

	}

	double fdm_interface::a1(pde* pde,double s, double t) const
	{		
        double alpha = (pde->diff_coeff())/(pow(m_dx, 2));
		double beta = (pde->conv_coeff())/(2*m_dx);
		
		return m_dt*(1-m_theta)*(beta-alpha);
	}

	double fdm_interface::a2(pde* pde, double s, double t) const
	{
 		double alpha = (pde->diff_coeff())/(pow(m_dx,2));
        return (1 - (1 - m_theta)*m_dt*(m_r->get_rate(s, t) - 2*alpha));
	}

	double fdm_interface::a3(pde* pde, double s, double t) const
	{
		double alpha = (pde->diff_coeff())/(pow(m_dx, 2));
		double beta=(pde->conv_coeff())/(2*m_dx);
		
		return (-m_dt*(1-m_theta)*(beta+alpha));
	}

	double fdm_interface::b1(pde* pde, double s, double t) const
	{
		double alpha = (pde->diff_coeff())/(pow(m_dx, 2));
		double beta=(pde->conv_coeff())/(2*m_dx);
		
		return m_dt*m_theta*(alpha - beta);
	}

	double fdm_interface::b2(pde* pde, double s, double t) const
	{
        double alpha = (pde->diff_coeff())/(pow(m_dx, 2));
			
        return (1 + m_theta*m_dt*(m_r->get_rate(s, t) - 2*alpha));
	}

	double fdm_interface::b3(pde* pde, double s, double t) const
	{
		double alpha = (pde->diff_coeff())/(pow(m_dx, 2));
		double beta=(pde->conv_coeff())/(2*m_dx);

		return m_dt*m_theta*(beta+alpha);
	}

	fdm::fdm(pde* pde, payoff* payoff, rate * r, double f0, double fN, int dt, int dx, double theta)
		: fdm_interface(pde, payoff, r, f0, fN, dt, dx, theta)
    {
    }

	fdm::~fdm()
	{
		m_pde = nullptr;
		m_r = nullptr;
		m_payoff = nullptr;
	}

	double fdm::get_price(pde* pde, interface* opt, payoff* payoff, Space_boundaries* sb, Time_boundaries* tb) const
	{
		//Calcul avec FDM
		//1. On discretise le temps et l'espace
        std::size_t T = tb->time_mesh(m_dt, opt);
        std::size_t N = sb->space_mesh(m_dx, opt);
		double r0 = opt->get_rate(); //dependency in t & s?

		//2. Calcul des f intermédiaires
			
		//Calcul de la matrice F en T
        std::vector<double> F(N-1, payoff->get_payoff(opt->get_spot()));
        	
		//Calcul des coeffs des matrices tridiagonales en T
		double mat = opt->get_maturity(); 
		//def matrice des coeffs pour avoir les coeffs en tt pt de l espace
		std::vector<double> A1(N-1, 0.0);
		std::vector<double> A2(N-1, 0.0);
		std::vector<double> A3(N-1, 0.0);
		std::vector<double> B1(N-1, 0.0);
		std::vector<double> B2(N-1, 0.0);
		std::vector<double> B3(N-1, 0.0);
				
		double Smax = sb->s_boundary_right(opt->get_spot(), opt->get_vol(), opt->get_maturity());
		double Tmax =  tb->t_boundary_right(opt->get_maturity());
		double Tmin =  tb->t_boundary_left(opt->get_maturity());

		double s = exp(Smax);

		for (std::size_t i=0; i < N-1; i++)
		{
			A1[i] = a1(pde, s, mat);
			A2[i] = a2(pde, s, mat);
			A3[i] = a3(pde, s, mat);
			B1[i] = b1(pde, s, mat);
			B2[i] = b2(pde, s, mat);
			B3[i] = b3(pde, s, mat);

			s -= m_dx;
		}

		//Calcul du terme constant que l'on extrait pour avoir une matrice tridiagonale
		std::vector<double> C(N-1, 0.0);

		C[0] = (A1[0] - B1[0]*exp(-r0*m_dt))*m_f0;

		C[N-2] = (A3[N-2] - B3[N-2]*exp(-r0*m_dt))*m_fN;

		//Calcul de la matrice D en T
		std::vector<double> D(N-1,0.0);
		D[0] = A2[0]*F[0] + A3[0]*F[1] + C[0];
		D[N-2] = A1[N-2]*F[N-2] + A2[N-2]*F[N-2] + C[N-2];
		
		for (std::size_t i=1; i < N-2; i++)
		{
			D[i] = A1[i]*F[i-1] + A2[i]*F[i] + A3[i]*F[i+1];
		}

		//On remonte via l'algorithme de Thomas
		s = exp(Smax);

		for (int t = T-1; t>=0 ; t--)
		{
			//Pour chaque x(i,t+1), en commencant par f_N, on remonte à x(i,t)
			//via l'algo de Thomas et le système trouvé
			
			//On calcule la matrice tridiagoname en t en tt pt de l'espace
			for (unsigned long i=0; i < N-1; i++)
			{
				A1[i] = a1(pde, s, t);
				A2[i] = a2(pde, s, t);
				A3[i] = a3(pde, s, t);
				B1[i] = b1(pde, s, t);
				B2[i] = b2(pde, s, t);
				B3[i] = b3(pde, s, t);

				s -= m_dx;
			}


			//On calcule la matrice F en t, et on fait Ft1=Ft
      			F = thomas(B1, B2, B3, D);
            
			//On recalcule la nouvelle matrice D
			D[0] = A2[0]*F[0] + A3[0]*F[1] + C[0];
			D[N-2] = A1[N-2]*F[N-2] + A2[N-2]*F[N-2] + C[N-2];
		
			for (unsigned long i=1; i < N-2; i++)
			{
				D[i] = A1[i]*F[i-1] + A2[i]*F[i] + A3[i]*F[i+1];
			}
		}

		return F[F.size() - 1];
	}


	std::vector<double> fdm::thomas(const std::vector<double> a,
                                    const std::vector<double> b,
                                    const std::vector<double> c,
                                    std::vector<double> d) const
	{
		//algo de Thomas pour inverser une matrice tridiagonale dans le cas de coeffs "constant" dans l'espace		
        std::size_t n = d.size();
		std::vector<double> new_coeffs(n, 0);
		std::vector<double> new_d(n, 0);  

		//Forward sweep
		new_coeffs[0] =c[0]/b[0];
		new_d[0] = d[0]/b[0];

		for (std::size_t i=1; i<n; i++)
		{
			new_coeffs[i] = c[i] / (b[i]-a[i] * new_coeffs[i-1]);
			new_d[i] = (d[i]-a[i] * new_d[i-1]) / (b[i]-a[i] * new_coeffs[i-1]);
		}
		
		//Back substitution
        std::vector<double> y(n, 0.0);
		y[n-1] = new_d[n-1];

		for (int i = n-2; i>=0; i--)
		{
			y[i] = new_d[i] - new_coeffs[i]*y[i+1];
		}

		return y;
	}

    double fdm::get_delta(pde* t_pde,
                     interface* opt,
                     payoff* payoff,
                     Space_boundaries* sb,
                     Time_boundaries* tb) const
    {
        double spot_plus = opt->get_spot() + 0.01;
        double spot_minus = opt->get_spot() - 0.01;
        
        interface* opt_plus = new interface(*opt);
        opt_plus->set_spot(spot_plus);
        
        interface* opt_minus = new interface(*opt);
        opt_minus->set_spot(spot_minus);
        
        pde* pde_plus = new bs_pde(opt_plus); // problème
        pde* pde_minus = new bs_pde(opt_minus);
        
        double p_plus = get_price(pde_plus, opt_plus, payoff, sb, tb);
        double p_minus = get_price(pde_minus, opt_minus, payoff, sb, tb);
        
        delete pde_plus;
        delete pde_minus;
        delete opt_plus;
        delete opt_minus;
        
        return (p_plus - p_minus)/0.02;
    }

    double fdm::get_gamma(pde* t_pde,
                     interface* opt,
                     payoff* payoff,
                     Space_boundaries* sb,
                     Time_boundaries* tb) const
    {
        double spot_plus = opt->get_spot() + 0.01;
        double spot_minus = opt->get_spot() - 0.01;
        
        interface* opt_plus = new interface(*opt);
        opt_plus->set_spot(spot_plus);
        
        interface* opt_minus = new interface(*opt);
        opt_minus->set_spot(spot_minus);
        
        pde* pde_plus = new bs_pde(opt_plus);
        pde* pde_minus = new bs_pde(opt_minus);
        
        double p = get_price(t_pde, opt, payoff, sb, tb);
        double p_plus = get_price(pde_plus, opt_plus, payoff, sb, tb);
        double p_minus = get_price(pde_minus, opt_minus, payoff, sb, tb);
        
        delete pde_plus;
        delete pde_minus;
        delete opt_plus;
        delete opt_minus;
        
        return (p_plus - 2*p + p_minus)/(0.01*0.01);
    }

    double fdm::get_theta(pde* t_pde,
                          interface* opt,
                          payoff* payoff,
                          Space_boundaries* sb,
                          Time_boundaries* tb) const
    {
        double t_plus = opt->get_maturity() + 0.003;
        double t_minus = opt->get_maturity() - 0.003;
        
        interface* opt_plus = new interface(*opt);
        opt_plus->set_maturity(t_plus);
        
        interface* opt_minus = new interface(*opt);
        opt_minus->set_maturity(t_minus);
        
        pde* pde_plus = new bs_pde(opt_plus);
        pde* pde_minus = new bs_pde(opt_minus);
        
        double p_plus = get_price(pde_plus, opt_plus, payoff, sb, tb);
        double p_minus = get_price(pde_minus, opt_minus, payoff, sb, tb);
        std::cout << p_plus << std::endl;
        std::cout << p_minus << std::endl;
        
        delete pde_plus;
        delete pde_minus;
        delete opt_plus;
        delete opt_minus;
        
        return (p_plus - p_minus)/0.006;
    }


    double fdm::get_vega(pde* t_pde,
                          interface* opt,
                          payoff* payoff,
                          Space_boundaries* sb,
                          Time_boundaries* tb) const
    {
        volatility* sig_plus = new vol_cst(opt->get_vol() + 0.01);
        volatility* sig_minus = new vol_cst(opt->get_vol() - 0.01);
        
        interface* opt_plus = new interface(*opt);
        opt_plus->set_vol(sig_plus);
        
        interface* opt_minus = new interface(*opt);
        opt_minus->set_vol(sig_minus);
        
        pde* pde_plus = new bs_pde(opt_plus);
        pde* pde_minus = new bs_pde(opt_minus);
        
        double p_plus = get_price(pde_plus, opt_plus, payoff, sb, tb);
        double p_minus = get_price(pde_minus, opt_minus, payoff, sb, tb);
        std::cout << p_plus << std::endl;
        std::cout << p_minus << std::endl;
        
        delete pde_plus;
        delete pde_minus;
        delete opt_plus;
        delete opt_minus;
        
        return (p_plus - p_minus)/0.02;
    }


}
