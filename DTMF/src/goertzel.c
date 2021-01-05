#include <stdint.h>
#include <math.h>

#include "debug.h"
#include "goertzel.h"

/*
 * goertzel initialize function, translated from the Figure 4 from HW Doc.
 */
void goertzel_init(GOERTZEL_STATE *gp, uint32_t N, double k) {
    (*gp).N = N;
    (*gp).k = k;
    (*gp).A = 2*M_PI*(k/N);
    (*gp).B = 2*cos(gp -> A);
    (*gp).s0 = 0;
    (*gp).s1 = 0;
    (*gp).s2 = 0;
}

/*
 * goertzel step function, translated from the Figure 4 from HW Doc.
 */
void goertzel_step(GOERTZEL_STATE *gp, double x) {
	(*gp).s0 = x + ((*gp).B)*((*gp).s1) - ((*gp).s2);
	(*gp).s2 = (*gp).s1;
	(*gp).s1 = (*gp).s0;
}

/*
 * goertzel strength function, translated from the Figure 4 from HW Doc.
 */
double goertzel_strength(GOERTZEL_STATE *gp, double x) {
    (*gp).s0 = x + ((*gp).B)*((*gp).s1) - (*gp).s2;
    double Y_sq, Y_r, Y_i, C_r, C_i, D_r, D_i, s_0, s_1;

    C_r = cos((*gp).A);
    C_i = -sin((*gp).A);
    D_r = cos( ((*gp).A)*((*gp).N -1) );
    D_i = -sin( ((*gp).A)*((*gp).N -1) );
    s_0 = (*gp).s0;
    s_1 = (*gp).s1;

	Y_r = ((s_0 - s_1*C_r)*D_r - (s_1*C_i*D_i));
	Y_i = ((s_0 - s_1*C_r)*D_i + (s_1*C_i*D_r));

	Y_sq = pow(Y_r, 2) + pow(Y_i, 2);


    return 2*Y_sq/pow((*gp).N, 2);
}
