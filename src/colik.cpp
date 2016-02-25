#include <RcppArmadillo.h>
// [[Rcpp::depends(RcppArmadillo)]]



//odeint
// [[Rcpp::depends(BH)]]
#include <iostream>
#include <vector>
#include <boost/numeric/odeint.hpp>

using namespace arma;
using namespace Rcpp; 

static const int SAMPLE  = 0; 
static const int CO = 1; 

typedef std::vector<double> state_type; 

mat finite_size_correction2(const vec& p_a, const vec& A, const std::vector<bool> extant, mat P)
{
	// NOTE mstates m X n 
	int u; 
	vec rho; 
	vec rterm; 
	//~ vec lterm; 
	double lterm; 
	vec p_u; 
	vec Amin_pu; 
	//~ for (int iu = 0; iu < extantLines.size(); iu++){
	for (int iu = 0; iu < extant.size(); iu++){
		if (extant.at(iu)){
			u = iu + 1; 
			p_u = P.col(u-1); 
			Amin_pu = clamp(( A - p_u), 1., INFINITY ); 
			rterm = p_a / Amin_pu ; 
			rho = A / Amin_pu; 
			lterm = dot( rho, p_a); //
			p_u = p_u % (lterm - rterm) ;
			p_u = p_u / sum(p_u ) ; 
			P.col(u - 1) = p_u; 
		}
	}
	return P; 
}


class DQAL{
	List Fs, Gs, Ys; 
	int m; 
	double hres; 
	double treeT; 
public:
	DQAL( List Fs, List  Gs, List Ys, int m, double hres, double treeT ) : Fs(Fs),Gs(Gs),Ys(Ys),m(m),hres(hres),treeT(treeT) {};
	void operator() ( const state_type &x , state_type &dxdt , double t)//, const double /* t */ )
    {
		// time index
		//~ int i =  (int)min( 1+(int)( hres * (*t) / treeT ), hres);
		//~ NOTE hres = length(times)
		//~ NOTE treeT = max(times)
		int i =  (int)std::max(0., std::min( hres * t / treeT , (double)(hres-1.))); //TODO redefine max min? will use arma versin? 
		mat F = as<mat>(Fs[i]); 
		mat G = as<mat>(Gs[i]); 
		vec Y = as<vec>(Ys[i]); 
		
		int k,l,z,w;
		
		double a[m]; //normalized nlft 
		//~ double sumA = 0.; 
		//~ for (k = 0; k < m; k++) sumA += A(k);
		double r = 1. ; // Atotal / sumA; // TODO
		for (k = 0; k < m; k++) { 
			//dA(k) = 0.;
			dxdt[Aind(k)] = 0.; 
			if (Y(k) > 0) {
				//~ a[k] = r *  std::min(1., A(x, k)/ Y(k) );
				a[k] = r *  A(x, k)/ Y(k) ;
			} else{
				a[k] = 1.; //
			} 
		}
		
		//dA
		// TODO shouldn't sum(A) be conserved over each interval?? then these are not the right eqns...
		for (k = 0; k < m; k++){
			for (l = 0; l < m; l++){
				if (k==l){
					//dA(k) -= a[l] * (F(i,l,k)) * a[k];
					dxdt[ Aind(k) ] -= a[l] * F(l,k) * a[k];
				} else{
					dxdt[ Aind(k) ]  += ( std::max(0., (1 - a[k])) * F(k,l) + G(k,l)) * a[l] ;
					dxdt[ Aind(k) ]  -= (F(l,k) + G(l,k)) * a[k];
				}
			}
		}
		//dQ
		for (z = 0; z < m; z++){ // col of Q
			for (k = 0; k < m; k++){ //row of Q
				dxdt[ Qind(k, z ) ] = 0. ; 
				for (l = 0. ; l < m; l++){
					if (k!=l){
						if ( Q(x, l,z) > 0)
						{
							dxdt[ Qind(k, z ) ] += (F(k,l) + G(k,l)) *  Q(x, l,z)/  std::max(Q(x,l,z), Y(l));
						}
						if (Q(x, k,z) > 0)
						{
							dxdt[ Qind(k, z ) ] -= (F(l,k) + G(l,k)) *  Q(x, k,z)/  std::max(Q(x, k,z), Y(k));
						}
					}
					// coalescent:
					//~ dQ(k,z) -= (F(i,k,l)+F(i,l,k)) * a[l] * Q(k,z)/Y(i,k);
					if (Q(x, k,z) > 0){
						dxdt[ Qind(k, z ) ] -= F(k,l) * a[l] * Q(x, k,z)/  std::max(Q(x, k,z), Y(k));
					}
				}
			}
		}
		//dL
		double dL = 0.;
		double Ydenom;
		for (k = 0; k < m; k++){
			for (l =0 ; l < m; l++){
				if (k==l){
					if (Y(k) > 1 && A(x,k) > 1){
						Ydenom = std::max( (Y(k)-1.),(r*A(x,k)-1.) );//
						if (Ydenom > 0)
						{
							dL += std::max(0.,std::min(1.,a[k])) * (  (r*A(x, k)-1)/Ydenom) * F(k,l);
						}
					} else{
						dL += std::max(0.,std::min(1.,a[k])) * std::max(0.,std::min(1.,a[l])) * F(k,l);
					}
				} else{
					dL += std::max(0.,std::min(1.,a[k])) * std::max(0.,std::min(1.,a[l])) * F(k,l);
				}
			}
		}
		dL = std::max(dL, 0.);
		dxdt[Lind()] = dL; 
    }
    
private:
	//~ double F(int ih, int k, int l){
		//~ return as<mat>(Fs[ih]).at(k,l);
	//~ }
	//~ double G(int ih, int k, int l){
		//~ return as<mat>(Gs[ih]).at(k,l);
	//~ }
	//~ double Y(int ih, int k){
		//~ return as<vec>(Ys[ih]).at(k);
	//~ }
	
	double Q( const state_type &x, int k, int l) {
		return x[l*m+k]; 
	}
	double A( const state_type &x, int k) {
		return x[(int)pow(m,2) + k]; 
	}
	double L( const state_type &x ) {
		return x[ (int)pow(m,2) + m] ;
	}
	
	int Qind( int k, int l ){
		return l*m + k ; 
	}
	int Aind( int k ){
		return (int)pow(m,2) + k; 
	}
	int Lind(){
		return (int)pow(m,2) + m ; 
	}
}; 


state_type generate_initial_conditions( vec A){
	int m = A.size() ;
	state_type x( (int)pow(m,2) + m + 1, 0. ); 
	int k = 0; 
	for (int i =0; i < m; i++){
		for (int j =0 ; j < m; j++){
			if (i == j){
				x[k] =1.; 
			} 
			k++; 
		}
	}
	for (int i = 0; i < m; i++){
		x[k] = A.at(i) ; 
		k++;
	}
	return x; 
}

void Q_from_state( mat &Q, state_type xfin ){
	int m = Q.n_rows; 
	int k =0 ;
	for (int i = 0; i < m; i++){
		for (int j = 0; j < m; j++){
			Q.at(i,j) = xfin[k]; 
			k++; 
		}
	}
	for (int i = 0; i < m; i++){
		Q.row(i) = Q.row(i) / sum(Q.row(i)); 
	}
	//~ Q = Q / arma::sum(Q, 1) ; 
}

void A_from_state( vec &A, state_type xfin){
	int m = A.size(); 
	int k = 0; 
	for ( int i = (int)pow(m,2); i < ((int)pow(m,2)+m); i++){
		A.at(k) = xfin[i]; 
		k++; 
	}
}

double L_from_state( state_type xfin){
	return (double)xfin[xfin.size()-1]; 
}

//[[Rcpp::export()]]
double colik2cpp(const NumericVector heights, const List Fs, const List Gs, const List Ys
  , const IntegerVector eventIndicator // sample or co
  , const IntegerVector eventIndicatorNode // node involved at each event
  , const NumericVector eventHeights
  , const mat sortedSampleStates
  , const IntegerMatrix daughters // daughters of each node
  , const int n
  , const int Nnode
  , const int m
  , bool AgtYboundaryCondition )
{
	double loglik = 0.;
	mat P(m, n + Nnode, fill::zeros); 
	int nextant = 0; 
	int u,v,w,z,a; 
	int samplesAdded = 0;
	mat Q  = zeros(m, m );
	vec A_Y;
	vec Y ;
	vec A = zeros(m);
	mat F = zeros(m,m);
	mat G = zeros(m,m);
	vec puY, pvY, pa;
	vec m_rs_R; 
	std::vector<bool> extant(n + Nnode, false); 
	
	// instantiate solver 
	double hres =  heights.size() ;
	double treeT = heights[heights.size()-1]; // note increasing order   
	DQAL dqal(Fs, Gs, Ys, m, hres, treeT ); 
	state_type x0;//, x; 
	//~ std::vector<state_type> x_vec;
	//~ std::vector<double> x_vec_times;
	
	// iterate events 
	double nextEventHeight = eventHeights(0); 
	int ievent = 0; 
	
	double sterm0, sterm1; 
	double h, h0, h1, hstar, dh; 
	int ih; // index of nextEventHeight
	double default_step_size = std::abs(heights[1] - heights[0]); 
	double L = 0.; 	
	
	h = 0.; 
	while( nextEventHeight != INFINITY ){
		
		if (nextEventHeight > h ){
			A = sum(P, 1);
			A = normalise(A)  * ((double)nextant);
			x0 = generate_initial_conditions(A) ; 
//~ cout << "solving..." << endl;
//~ cout << h << endl;
//~ cout << nextEventHeight << endl;
//~ cout << A << endl;
//~ Rf_PrintValue( wrap(x0)); 
			//~ size_t steps = boost::numeric::odeint::integrate( dqal ,  x0 , h , nextEventHeight , default_step_size, simple_observer(x)  );  
			size_t steps = boost::numeric::odeint::integrate( dqal ,  x0 , h , nextEventHeight 
			  , std::min( default_step_size,  (nextEventHeight-h)/10.) );  
			//~ size_t steps = boost::numeric::odeint::integrate( dqal ,  x0 , h , nextEventHeight , default_step_size
			  //~ , push_back_state_and_time( x_vec, x_vec_times)  );  
			//~ size_t steps = boost::numeric::odeint::integrate( dqal ,  x0 , h , nextEventHeight , (nextEventHeight-h)/10.
			  //~ , push_back_state_and_time( x_vec, x_vec_times)  );  
//~ Rf_PrintValue( wrap(x0)); 
//~ cout << steps << endl;
//~ throw 1; 
			// 
			Q_from_state(Q, x0); 
			A_from_state(A, x0); 
			L = L_from_state(x0); 
			A = normalise(A) * ((double)nextant);
			
			P  = abs(Q.t() * P);
//~ cout << P.n_rows << " " << P.n_cols << endl; 
//~ cout << P.t() ; 
		} else{
			L = 0.; 
		}
		
		loglik -= std::max(0.,L);
		
		if (eventIndicator(ievent)==SAMPLE)
		{
			u = eventIndicatorNode(ievent); 
			nextant++; 
			P.col(u-1) = normalise(sortedSampleStates.col(samplesAdded));
			extant.at(u-1) = true; 
			samplesAdded++; 
		} else{
			// coalescent
			ih = (int)std::min( hres * nextEventHeight / treeT, (double)(heights.size()-1)); 
			F = as<mat>(Fs[ih]);
			G = as<mat>(Gs[ih]);
			Y = as<vec>(Ys[ih]);
//~ if (sum(A) > sum(Y)) {
	//~ cout << Y ; 
	//~ cout << nextEventHeight << " " << ih << endl; 
	//~ cout << " sum A > sum Y  " << endl ;
//~ }
			if ( AgtYboundaryCondition && (sum(A) > sum(Y)) ) return -INFINITY;
			
			a = eventIndicatorNode(ievent);
			u = daughters(a -1 , 0); 
			v = daughters( a - 1, 1 ); 
			puY = normalise(  arma::min(Y,P.col(u- 1 )) ) / arma::clamp(Y, 1e-6, INFINITY ) ; 
			pvY = normalise(  arma::min(Y,P.col(v- 1 )) ) / arma::clamp( Y, 1e-6, INFINITY ) ; 
			//~ loglik += log( (( puY * F) * pvY).at(0,0) ) ; 
//~ double llterm = log(  as_scalar( puY.t() * (F * pvY) )  + as_scalar( pvY.t() * (F * puY) ) ) ;   
//~ if (llterm==-INFINITY)
//~ {
	//~ cout << " F " << endl; 
	//~ cout << F; 
	//~ cout << " G " << endl; 
	//~ cout << G;
	//~ cout << " Y " << endl; 
	//~ cout << Y;
	//~ cout << " puy " << endl; 
	//~ cout << puY; 
	//~ cout << " pvy " << endl; 
	//~ cout << pvY;  
	//~ cout << nextEventHeight << " " << ih << endl; 
	//~ cout << heights.size() << endl; 
	//~ cout << hres  << endl; 	
	//~ cout << treeT  << endl; 	
//~ }
//~ cout << " co event " << endl; 
//~ cout << puY; 
//~ cout << pvY; 
//~ cout << F;
			loglik += log(  as_scalar( puY.t() * (F * pvY) )  + as_scalar( pvY.t() * (F * puY) ) ) ;   
			// state of ancestor 
			pa =  arma::normalise( (F * puY) % pvY + (F * pvY) % puY) ; 
			//~ pa = pa / sum(pa ) ; 
			P.col(a - 1 ) = pa; 
//~ cout << loglik << endl; 
//~ cout << pa; 
//~ cout << endl << endl ; 
//~ if (any(is_nan(as<NumericVector>(wrap(pa))))) throw 1; 
			P = finite_size_correction2(pa, A, extant, P);
			
			//bookkeeping
			nextant--; 
			extant.at(u-1) = false;
			extant.at(v-1) = false;
			extant.at(a-1) = true;
			P.col(u-1) = zeros<colvec>(P.n_rows); 
			P.col(v-1) = zeros<colvec>(P.n_rows); 
		}
//~ cout << endl;
//~ cout << endl;
//~ cout << endl;
//~ cout << h << endl;
//~ cout << nextEventHeight << endl;
//~ cout << L << endl;
//~ cout << loglik << endl;
//~ cout << A << endl;
//~ cout << Q << endl;

//~ if (h > 1) throw 1; 
		// prep next iter
		h = nextEventHeight; 
		ievent++;
		if (ievent<eventHeights.size()){
			nextEventHeight = eventHeights(ievent);
		} else{
			nextEventHeight = INFINITY; 
		}
	}
	
	return loglik; 
}

