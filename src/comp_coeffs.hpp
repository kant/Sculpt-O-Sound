#ifndef _COMP_COEFFS_HPP
#define _COMP_COEFFS_HPP

#include "std.hpp"
#include <string.h>

void comp_coeffs_for_freq_and_bandwidth(float bandwidth, float center_frequency, double fsamp, float *alpha1, float *alpha2, float *beta);
void comp_coeffs_for_freqs(float lower_freq, float upper_freq, double fsamp, float *alpha1, float *alpha2, float *beta);
void comp_coeffs(int freqIndex, int freq[NR_OF_BANDS], float bandwidth, double fsamp, float *alpha1, float *alpha2, float *beta);
void print_coeffs(float alpha1, float alpha2, float beta);

void comp_all_coeffs(int freq[NR_OF_BANDS], float bandwidth, double fsamp, float alpha1[NR_OF_BANDS], float alpha2[NR_OF_BANDS], float beta[NR_OF_BANDS]);
void print_all_coeffs(float alpha1[], float alpha2[], float beta[]);

void init_pan_and_level(float startLevel[NR_OF_BANDS], float left_pan[NR_OF_BANDS], float right_pan[NR_OF_BANDS], float left_level[NR_OF_BANDS], float right_level[NR_OF_BANDS]);

void comp_release_times(float release_time[NR_OF_BANDS]);
void comp_attack_times(float attack_time[NR_OF_BANDS]);
void print_times(const std::string s, float times[NR_OF_BANDS]);
void print_array(float elements[NR_OF_BANDS]);

void comp_attack_factors(float envelope_attack_factor[NR_OF_BANDS], float envelope_attack_time[NR_OF_BANDS]);
void comp_release_factors(float envelope_release_factor[NR_OF_BANDS], float envelope_release_time[NR_OF_BANDS]);

#endif
