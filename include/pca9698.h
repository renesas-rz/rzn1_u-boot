/*
 * (C) Copyright 2011
 * Dirk Eibach,  Guntermann & Drunck GmbH, eibach@gdsys.de
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __PCA9698_H_
#define __PCA9698_H_

int pca9698_request(unsigned gpio, const char *label);
void pca9698_free(unsigned gpio);
int pca9698_direction_input(u8 addr, unsigned gpio);
int pca9698_direction_output(u8 addr, unsigned gpio, int value);
int pca9698_get_value(u8 addr, unsigned gpio);
int pca9698_set_value(u8 addr, unsigned gpio, int value);

int  pca9698_set_all(u8 addr, u8 direction[5], u8 value[5]);
u64  pca9698_read_all(u8 addr);

#endif /* __PCA9698_H_ */
