/* $Id: complex_vector.h,v 1.2 2005-06-24 17:41:52 acs Exp $
   Written by Adam Siepel, Summer 2005
   Copyright 2005, Adam Siepel, University of California
*/

/** \file complex_vector.h
    Vectors of complex numbers
    \ingroup base
*/

#ifndef ZVEC_H
#define ZVEC_H

#include <complex.h>
#include <stdio.h>

/** Structure for vector of complex numbers -- array of Complex
    objects and its length */
typedef struct { 
  Complex *data;		/**< array of Complex objects */ 
  int size;			/**< length of array */ 
} Zvector; 

Zvector *zvec_new(int size);
void zvec_free(Zvector *v);
Complex zvec_get(Zvector *v, int i);
void zvec_set(Zvector *v, int i, Complex val);
void zvec_set_all(Zvector *v, Complex val);
void zvec_copy(Zvector *dest, Zvector *src);
Zvector* zvec_create_copy(Zvector *src);
void zvec_print(Zvector *v, FILE *F);
void zvec_read(Zvector *v, FILE *F);
Zvector *zvec_new_from_file(FILE *F, int size);
void zvec_zero(Zvector *v);
void zvec_plus_eq(Zvector *thisv, Zvector *addv);
void zvec_minus_eq(Zvector *thisv, Zvector *subv);
void zvec_scale(Zvector *v, double scale_factor);

/***************************************************************************
 * inline functions; also defined in complex_vector.c 
 ***************************************************************************/

/* we'll only inline the functions likely to be used heavily in inner
   loops */  

extern inline
Complex zvec_get(Zvector *v, int i) { /* check */
  return v->data[i];
}

extern inline
void zvec_set(Zvector *v, int i, Complex val) {
  v->data[i] = val;
}

#endif