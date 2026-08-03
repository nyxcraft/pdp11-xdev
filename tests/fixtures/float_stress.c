/*
 * Floating-point stress fixture for the 2.8BSD PDP-11 toolchain.
 *
 * A single, non-trivial numerical program -- not micro-cases -- that drives
 * the whole DEC F/D floating-point path end to end: double function args and
 * returns, recursion returning double, float arrays, deeply iterated loops,
 * float comparisons, mixed int/float arithmetic, and %f/%e/%g formatting over
 * a wide range of magnitudes.  Compiled by cc, run in apsim; its stdout is
 * compared against a golden in tests/cc/float_stress.sh.  K&R C throughout.
 */

double mysqrt(x) double x; {		/* Newton's method */
	double g, prev;
	int i;
	if (x <= 0.0) return 0.0;
	g = x;
	for (i = 0; i < 60; i++) {
		prev = g;
		g = (g + x / g) / 2.0;
		if (g - prev < 0.0000001 && prev - g < 0.0000001) break;
	}
	return g;
}

double fact(n) int n; {			/* recursion returning double */
	if (n <= 1) return 1.0;
	return (double)n * fact(n - 1);
}

double approx_e() {			/* e = sum 1/n! */
	double sum;
	int n;
	sum = 0.0;
	for (n = 0; n < 15; n++) sum = sum + 1.0 / fact(n);
	return sum;
}

double poly(x) double x; {		/* Horner: 2x^3 - 3x^2 + 5x - 7 */
	return ((2.0 * x - 3.0) * x + 5.0) * x - 7.0;
}

int main() {
	double data[8], sum, mean, var, d, harm, pi4, t;
	int i, n;

	printf("sqrt: %f %f %f\n", mysqrt(2.0), mysqrt(16.0), mysqrt(2.25));
	printf("fact5=%f e=%f\n", fact(5), approx_e());
	printf("poly: %f %f\n", poly(2.0), poly(-1.0));

	/* array stats: data[i] = i*1.5; mean, variance, standard deviation */
	n = 8;
	sum = 0.0;
	for (i = 0; i < n; i++) { data[i] = i * 1.5; sum = sum + data[i]; }
	mean = sum / n;
	var = 0.0;
	for (i = 0; i < n; i++) { d = data[i] - mean; var = var + d * d; }
	var = var / n;
	printf("stats: %f %f %f\n", mean, var, mysqrt(var));

	/* 10000-term harmonic sum (precision under many small adds) */
	harm = 0.0;
	for (i = 1; i <= 10000; i++) harm = harm + 1.0 / (double)i;
	printf("harm=%f\n", harm);

	/* Leibniz pi/4 = 1 - 1/3 + 1/5 - ...  (5000 alternating terms) */
	pi4 = 0.0;
	t = 1.0;
	for (i = 0; i < 5000; i++) {
		pi4 = pi4 + t / (double)(2 * i + 1);
		t = -t;
	}
	printf("pi=%f\n", pi4 * 4.0);

	/* magnitude extremes + formatting */
	printf("mag: %e %e %e\n", 1.0e30, 2.0e-30, 1.0e30 * 2.0e-30);
	printf("fmt: %g %g %g %g\n", 0.0001, 100000.0, 3.14159, 0.0);
	return 0;
}
