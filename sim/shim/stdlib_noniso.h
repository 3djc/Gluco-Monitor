#pragma once
#ifdef __cplusplus
extern "C" {
#endif
char *itoa(int val, char *s, int radix);
char *ltoa(long val, char *s, int radix);
char *lltoa(long long val, char *s, int radix);
char *utoa(unsigned int val, char *s, int radix);
char *ultoa(unsigned long val, char *s, int radix);
char *ulltoa(unsigned long long val, char *s, int radix);
char *dtostrf(double val, signed int width, unsigned int prec, char *s);
#ifdef __cplusplus
}
#endif
