// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller,Ernesto Corvi
#include <cstdio>

#define CF  0x100
#define HF  0x200
#define DF  0x400

int dab[0x800];

int main(int ac, char **av)
{
	int i;

	for (i = 0; i < DF; i++) {
		int val = i & 0xff;
		int result;

		/* add/adc: correct the low digit if it overflowed or a half
		   carry was produced, the high digit if it overflowed or a
		   carry was produced; the high correction is the carry out */
		result = val;
		if ((i & HF) || (val & 0x0f) > 0x09)
			result += 0x06;
		if ((i & CF) || val > 0x99) {
			result += 0x60;
			dab[i] = CF | (result & 0xff);
		} else {
			dab[i] = result & 0xff;
		}

		/* sub/sbc: undo the binary borrow(s); the carry out is the
		   carry in, since no new borrow can be generated here */
		result = val;
		if (i & HF)
			result += 0xfa;
		if (i & CF)
			dab[DF+i] = CF | ((result + 0xa0) & 0xff);
		else
			dab[DF+i] = result & 0xff;
	}

	printf("// license:BSD-3-Clause\n");
	printf("// copyright-holders:Juergen Buchmueller,Ernesto Corvi\n");
	printf("/************************************************ \n");
	printf(" * Result table for Z8000 DAB instruction         \n");
	printf(" *                                                \n");
	printf(" * bits    description                            \n");
	printf(" * ---------------------------------------------- \n");
	printf(" * 0..7    destination value                      \n");
	printf(" * 8       carry flag before                      \n");
	printf(" * 9       half carry flag before                 \n");
	printf(" * 10      D flag (0 add/adc, 1 sub/sbc)          \n");
	printf(" *                                                \n");
	printf(" * result  description                            \n");
	printf(" * ---------------------------------------------- \n");
	printf(" * 0..7    result value                           \n");
	printf(" * 8       carry flag after                       \n");
	printf(" ************************************************/\n");
	printf("static const uint16_t Z8000_dab[0x800] = {\n");
	for (i = 0; i < 0x800; i++) {
		if ((i & 0x3ff) == 0) {
			if (i & 0x400)
				printf("\t/* sub/sbc results */\n");
			else
				printf("\t/* add/adc results */\n");
		}
		if ((i & 7) == 0) printf("\t");
		printf("0x%03x,",dab[i]);
		if ((i & 7) == 7) printf("\n");
	}
	printf("};\n");

	return 0;
}
