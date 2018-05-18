#ifndef INTEGRATE_H
#define INTEGRATE_H

#define BEST_FINENESS 1e-12

inline double f(double x)
{
	return 4 * x * x * x;
}

struct Connection makeConnection(int rd, int wr);

void childCalcSums(int rd, int wr);
double parentIntegrate(struct Connection* con, int nChildren, double left, double right, double maxDeviation, enum ErrorCode* error);

#endif
