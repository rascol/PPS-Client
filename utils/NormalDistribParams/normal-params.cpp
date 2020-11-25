/*
 * normalDistribParams.cpp
 *
 * Created on: Oct 20, 2016
 * Modified: Jan 1, 2018
 * Copyright (C) 2018 Raymond S. Connell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
unsigned int idnum = 1839762001;		// 1839762001
double pi = 3.14159265359;

const char *version = "2.0.0";

/**
 * Generates pseudo-random number in range
 * [low,high).
 *
 * @param low The low end of range including
 * the value low.
 * @param high The high end of the range not
 * including the value high.
 *
 * @returns The random value.
 */
double randomVar(double low, double high)
{
	double val;

	val = (double)rand() / 2147483648.0;

	if (low == 0.0 && high == 1.0)
		return (double)val;

	return (double)(val * (high - low) + low);
}

/**
 * Calculates the center of mass along x from
 * sample bins at x1 and x2.
 *
 * @param y1 Number of samples in the first bin.
 * @param x1 Bin index of first bin.
 * @param y2 Number of samples in the second bin.
 * @param x2 Bin index of second bin.
 *
 * @returns The center of mass
 */
double getCenterOfMass(double y1, double x1, double y2, double x2){
	double cm = (y1 * x1 + y2 * x2) / (y1 + y2);
	return cm;
}

/**
 * Calculates the median and standard deviation from three
 * consecutive values of a sample distribution binned
 * at unit intervals using Monte Carlo simulation and
 * the error function approximation to the cumulative
 * normal distribution.
 *
 * Random values of of median and stddev are tried until
 * (x1,y1), (x2,y2) and (x3,y3) fit the normal distribution
 * passing through the points as closely as possible within
 * the number of trials.
 *
 * Each bin extends over range [n - 0.5, n + 0.5).
 *
 * @param[in] y1 Number of samples in the first bin.
 * @param[in] x1 Bin index of the first bin.
 * @param[in] y2 Number of samples in the second bin.
 * @param[in] x2 Bin index of the second bin.
 * @param[in] y3 Number of samples in the third bin.
 * @param[in] x3 Bin index of the third bin.
 * @param[out] median Calculated median.
 * @param[out] stddev Calculated standard deviation.
 * @param[in] Y_total Total number of distribution samples.
 *
 * @returns The simulation error as the average error
 * between the three sample points and the best fit normal
 * distribution.
 */
double getNormalParams(double y1, double x1, double y2, double x2, double y3, double x3, double *median, double *stddev, double Y_total){
	double m, sd, error1, error2, error3;
	double s11, s12, s21, s22, s31, s32;
	double denom, d, width, halfBin, offset;

	offset = x1;
	x1 = 0;
	x2 = x2 - offset;
	x3 = x3 - offset;

	width = x2 - x1;
	halfBin = width * 0.5;

	double root2 = sqrt(2.0);

	double best_mean = 0.0, best_sd = 0.0;

	double min_d = 1e6;
	double rng = 1.5 * width;

	double r1 = 2.0 * y1 / Y_total;					// Relative bin weights pre-scaled by 2 to match erf()
	double r2 = 2.0 * y2 / Y_total;
	double r3 = 2.0 * y3 / Y_total;

	for (int i = 0; i < 1000000; i++){

		m = best_mean + randomVar(-rng, rng);		// Trial median in range 2 * rng
		sd = best_sd + randomVar(-rng, rng);		// Trial SD in range 2 * rng

		denom = 1.0 / (root2 * sd);

		s11 = (x1 - halfBin - m) * denom;			// Upper and lower limits of first bin
		s12 = (x1 + halfBin - m) * denom;

		error1 = (erf(s12) - erf(s11)) - r1;		// 2 * difference between ideal and measured for bin1

		s21 = (x2 - halfBin - m) * denom;			// Upper and lower limits of second bin
		s22 = (x2 + halfBin - m) * denom;

		error2 = (erf(s22) - erf(s21)) - r2;		// 2 * difference between ideal and measured for bin2

		s31 = (x3 - halfBin - m) * denom;			// Upper and lower limits of third bin
		s32 = (x3 + halfBin - m) * denom;

		error3 = (erf(s32) - erf(s31)) - r3;		// 2 * difference between ideal and measured for bin3

		d = sqrt((error1 * error1 + error2 * error2 + error3 * error3) / 3.0);

		if (d < min_d){
			min_d = d;
			best_mean = m;
			best_sd = sd;
		}

		rng *= 0.999995;
	}

	*median = best_mean + offset;
	*stddev = best_sd;

	return min_d / 2.0;								// Fractional area difference between the ideal Gaussian area
}													// and the measured area over a range of x1 - 0.5 to x3 + 0.5.

int main(int argc, char *argv[]){

	if (argc == 5){
		double Y1, x1, Y2, x2;
		sscanf(argv[1], "%lf", &x1);
		sscanf(argv[2], "%lf", &Y1);
		sscanf(argv[3], "%lf", &x2);
		sscanf(argv[4], "%lf", &Y2);

		double cm = getCenterOfMass(Y1, x1, Y2, x2);

		printf("Center of mass of the pair of points is %lf.\n", cm);
		return 0;
	}

	if ((argc == 1) || !(argc == 7 || argc == 8)){
		printf("Requires either two or three successive sample pairs.\n\n");

		printf("If two sample pairs, x1 y1 x2 y2, calculates the center\n");
		printf("of mass along x.\n\n");

		printf("If three successive sample pairs, calculates normal distribution\n");
		printf("parameters for x1 Y1 x2 Y2 x3 Y3, with uniform x separations that\n");
		printf("wrap the peak of the distribution near the maximum.\n\n");

		printf("Also accepts a seventh arg that specifies the sample size. Otherwise\n");
		printf("the y values are normalized to the default sample size of 86,400.\n\n");

		printf("Prints the median of an ideal normal distribution that best fits the\n");
		printf("three points, then the standard deviation of the best fit ideal\n");
		printf("distribution, then the relative sample fit to that ideal distribution.\n\n");

//		printf("In case the distribution was treated as half-normal, also prints the\n");
//		printf("half-normal median.\n\n");
		return 0;
	}

	double Y1, x1, Y2, x2, Y3, x3;
	sscanf(argv[1], "%lf", &x1);
	sscanf(argv[2], "%lf", &Y1);
	sscanf(argv[3], "%lf", &x2);
	sscanf(argv[4], "%lf", &Y2);
	sscanf(argv[5], "%lf", &x3);
	sscanf(argv[6], "%lf", &Y3);

	double dx1 = x2 - x1;
	double dx2 = x3 - x2;

	if (fabs(dx1 - dx2) > 1e-9 || dx1 < 0.0 || dx2 < 0.0){
		printf("Error: the x values must be uniformly spaced and increasing.\n");
		return -1;
	}

	double median, sd;
//	double half_median;

	double Y_total = 86400.0;
	if (argc == 8){
		sscanf(argv[7], "%lf", &Y_total);
	}

//	double relative_error = getNormalParams(Y1, x1, Y2, x2, Y3, x3, &median, &sd, Y_total);

//	half_median = (sd * 0.67448975019) + median; 			// The constant was pre-calculated as sqrt(2) * inv_erf(0.5)

	printf("Relative to the best fit normal distribution:\n");
	printf("maximum:  %lf\n", median);
	printf("stddev: %lf\n", sd);
//	printf("Relative fit of samples: %lf\n", 1.0 - relative_error);
//	printf("half-normal median: %lf\n", half_median);
	return 0;
}
