void sinx_taylor(int num_elements, int terms, double* x, double* result)

{
	for(int i=0; i<num_elements; i++) {
		double value = x[i];
		double numer= x[i] * x[i] * x[i];
		double denom= 6.; // 3!
		int sign = -1;
		for(int j=1; j<=terms; j++) {
 			value += (double)sign * numer/ denom;
 			numer*= x[i] * x[i];
 			denom*= (2.*(double)j+2.) * (2.*(double)j+3.);
 			sign *= -1;
 		}
 		result[i] = value;
 	}
 }

