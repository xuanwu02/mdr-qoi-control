#include <iostream>
#include <ctime>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <bitset>
#include <numeric>
#include "utils.hpp"
#include "qoi_utils.hpp"
#include "Reconstructor/Reconstructor.hpp"
#include "Synthesizer4GE.hpp"

using namespace MDR;

std::vector<double> P_ori;
std::vector<double> D_ori;
std::vector<double> Vx_ori;
std::vector<double> Vy_ori;
std::vector<double> Vz_ori;
double * P_dec = NULL;
double * D_dec = NULL;
double * Vx_dec = NULL;
double * Vy_dec = NULL;
double * Vz_dec = NULL;
double * V_TOT_ori = NULL;
double * Temp_ori = NULL;
double * C_ori = NULL;
double * Mach_ori = NULL;
double * PT_ori = NULL;
double * mu_ori = NULL;
std::vector<double> error_V_TOT;
std::vector<double> error_Temp;
std::vector<double> error_C;
std::vector<double> error_Mach;
std::vector<double> error_PT;
std::vector<double> error_mu;
std::vector<double> error_est_V_TOT;
std::vector<double> error_est_Temp;
std::vector<double> error_est_C;
std::vector<double> error_est_Mach;
std::vector<double> error_est_PT;
std::vector<double> error_est_mu;


template<class T>
bool halfing_error_C_uniform(const T * P, const T * D, size_t n, const double tau, std::vector<double>& ebs){
	double eb_P = ebs[0];
	double eb_D = ebs[1];
	double R = 287.1;
	double gamma = 1.4;
	double c_1 = 1.0 / R;
	double c_2 = sqrt(gamma * R);
	double max_value = 0;
	int max_index = 0;
	for(int i=0; i<n; i++){
		double e_T = c_1 * compute_bound_division(P[i], D[i], eb_P, eb_D);
		double Temp = P[i] / (D[i] * R);
		// error of C
		double e_C = c_2*compute_bound_square_root_x(Temp, e_T);
		double C = c_2 * sqrt(Temp);

		error_est_C[i] = e_C;
		error_C[i] = C - C_ori[i];

		if(max_value < error_est_C[i]){
			max_value = error_est_C[i];
			max_index = i;
		}

	}
	std::cout << "P = " << P[max_index] << " D = " << D[max_index] << std::endl;
	std::cout << "eb_P = " << eb_P << " eb_D = " << eb_D << std::endl;
	std::cout << "coeff_P = " << fabs(P[max_index])*eb_D << " coeff_D = " << fabs(D[max_index])*eb_P << std::endl;
	std::cout << names[2] << ": max estimated error = " << max_value << ", index = " << max_index << std::endl;
	// estimate error bound based on maximal errors
	if(max_value > tau){
		auto i = max_index;
		double estimate_error = max_value;
        double eb_P = ebs[0];
        double eb_D = ebs[1];
		while(estimate_error > tau){
    		std::cout << "uniform decrease\n";
            eb_P = eb_P / 1.5;
            eb_D = eb_D / 1.5;
			double e_T = c_1 * compute_bound_division(P[i], D[i], eb_P, eb_D);
			double Temp = P[i] / (D[i] * R);
			estimate_error = c_2*compute_bound_square_root_x(Temp, e_T);
		}
        ebs[0] = eb_P;
        ebs[1] = eb_D;
		return false;
	}
	return true;
}


int main(int argc, char ** argv){

    using T = double;
    double target_rel_eb = atof(argv[1]);

    size_t num_elements = 0;
    P_ori = MGARD::readfile<T>((data_file_prefix + "Pressure.dat").c_str(), num_elements);
    D_ori = MGARD::readfile<T>((data_file_prefix + "Density.dat").c_str(), num_elements);
    Vx_ori = MGARD::readfile<T>((data_file_prefix + "VelocityX.dat").c_str(), num_elements);
    Vy_ori = MGARD::readfile<T>((data_file_prefix + "VelocityY.dat").c_str(), num_elements);
    Vz_ori = MGARD::readfile<T>((data_file_prefix + "VelocityZ.dat").c_str(), num_elements);
    std::vector<double> ebs;
    ebs.push_back(compute_value_range(P_ori));
    ebs.push_back(compute_value_range(D_ori));
	int n_variable = ebs.size();

    for(int i=0; i<ebs.size(); i++){
    	ebs[i] *= target_rel_eb;
    }

    std::vector<T> V_TOT(num_elements);
    std::vector<T> Temp(num_elements);
    std::vector<T> C(num_elements);
    std::vector<T> Mach(num_elements);
    std::vector<T> PT(num_elements);
    std::vector<T> mu(num_elements);
    compute_QoIs(Vx_ori.data(), Vy_ori.data(), Vz_ori.data(), P_ori.data(), D_ori.data(), num_elements, V_TOT.data(), Temp.data(), C.data(), Mach.data(), PT.data(), mu.data());

    std::vector<double> tau;
    tau.push_back(compute_value_range(V_TOT)*target_rel_eb);
    tau.push_back(compute_value_range(Temp)*target_rel_eb);
    tau.push_back(compute_value_range(C)*target_rel_eb);
    tau.push_back(compute_value_range(Mach)*target_rel_eb);
    tau.push_back(compute_value_range(PT)*target_rel_eb);
    tau.push_back(compute_value_range(mu)*target_rel_eb);
	V_TOT_ori = V_TOT.data();
	Temp_ori = Temp.data();
	C_ori = C.data();
	Mach_ori = Mach.data();
	PT_ori = PT.data();
	mu_ori = mu.data();

    std::string mask_file = rdata_file_prefix + "mask.bin";
    size_t num_valid_data = 0;
    auto mask = MGARD::readfile<unsigned char>(mask_file.c_str(), num_valid_data);
    std::vector<MDR::ComposedReconstructor<T, MGARDHierarchicalDecomposer<T>, DirectInterleaver<T>, PerBitBPEncoder<T, uint32_t>, AdaptiveLevelCompressor, SignExcludeGreedyBasedSizeInterpreter<MaxErrorEstimatorHB<T>>, MaxErrorEstimatorHB<T>, ConcatLevelFileRetriever>> reconstructors;
    for(int i=0; i<n_variable; i++){
        // std::string rdir_prefix = rdata_file_prefix + "zone_" + id_str + "_" + varlist[i];
        std::string rdir_prefix = rdata_file_prefix + varlist[i+3];
        std::string metadata_file = rdir_prefix + "_refactored_data/metadata.bin";
        std::vector<std::string> files;
        int num_levels = 9;
        for(int i=0; i<num_levels; i++){
            std::string filename = rdir_prefix + "_refactored_data/level_" + std::to_string(i) + ".bin";
            files.push_back(filename);
        }
        auto decomposer = MGARDHierarchicalDecomposer<T>();
        auto interleaver = DirectInterleaver<T>();
        auto encoder = PerBitBPEncoder<T, uint32_t>();
        auto compressor = AdaptiveLevelCompressor(64);
        auto estimator = MaxErrorEstimatorHB<T>();
        auto interpreter = SignExcludeGreedyBasedSizeInterpreter<MaxErrorEstimatorHB<T>>(estimator);
        auto retriever = ConcatLevelFileRetriever(metadata_file, files);
        reconstructors.push_back(generateReconstructor<T>(decomposer, interleaver, encoder, compressor, estimator, interpreter, retriever));
        reconstructors.back().load_metadata();
    }    
    std::vector<std::vector<T>> reconstructed_vars(n_variable, std::vector<double>(num_elements));
	std::vector<size_t> total_retrieved_size(n_variable, 0);

    int iter = 0;
    int max_iter = 5;
	bool tolerance_met = false;
    while((!tolerance_met) && (iter < max_iter)){
    	iter ++;
	    for(int i=0; i<n_variable; i++){
	        auto reconstructed_data = reconstructors[i].progressive_reconstruct(ebs[i], -1);
	        memcpy(reconstructed_vars[i].data(), reconstructed_data, num_elements*sizeof(T));
			total_retrieved_size[i] = reconstructors[i].get_retrieved_size();
	    }
	    P_dec = reconstructed_vars[0].data();
	    D_dec = reconstructed_vars[1].data();
	    MGARD::print_statistics(P_ori.data(), P_dec, num_elements);
	    MGARD::print_statistics(D_ori.data(), D_dec, num_elements);
	    error_C = std::vector<double>(num_elements);
	    error_est_C = std::vector<double>(num_elements);
		std::cout << "iter" << iter << ": The old ebs are:" << std::endl;
	    MDR::print_vec(ebs);
	    tolerance_met = halfing_error_C_uniform(P_dec, D_dec, num_elements, tau[2], ebs);
		std::cout << "iter" << iter << ": The new ebs are:" << std::endl;
	    MDR::print_vec(ebs);
	    std::cout << names[2] << " requested error = " << tau[2] << std::endl;
	    print_max_abs(names[2] + " error", error_C);
	    print_max_abs(names[2] + " error_est", error_est_C);   	
    }
	std::cout << "iter = " << iter << std::endl;
   
   	size_t total_size = std::accumulate(total_retrieved_size.begin(), total_retrieved_size.end(), 0);
	double cr = n_variable * num_elements * sizeof(T) * 1.0 / total_size;
	std::cout << "each retrieved size:" << std::endl;
	MDR::print_vec(total_retrieved_size);
	std::cout << "aggregated cr = " << cr << std::endl;

    return 0;
}