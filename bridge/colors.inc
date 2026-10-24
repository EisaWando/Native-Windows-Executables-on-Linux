/* Fixed classic theme profile; HLS uses SHLWAPI's 0..240 scale. */
__attribute__((ms_abi)) int32_t DwmGetColorizationColor(DWORD *color,
							BOOL *opaque)
{
	if (!color || !opaque)
		return (int32_t)0x80070057;
	*color = 0xff0078d4;
	*opaque = 1;
	return 0;
}
__attribute__((ms_abi)) int32_t DwmIsCompositionEnabled(BOOL *enabled)
{
	if (!enabled)
		return (int32_t)0x80070057;
	*enabled = 0;
	return 0;
}
__attribute__((ms_abi)) BOOL IsThemeActive(void)
{
	return 0;
}
__attribute__((ms_abi)) BOOL IsAppThemed(void)
{
	return 0;
}
__attribute__((ms_abi)) DWORD GetThemeAppProperties(void)
{
	return 0;
}
__attribute__((ms_abi)) void ColorRGBToHLS(DWORD color, uint16_t *h,
					   uint16_t *l, uint16_t *s)
{
	int r = color & 255, g = (color >> 8) & 255, b = (color >> 16) & 255,
	    max = r, min = r;
	if (g > max)
		max = g;
	if (b > max)
		max = b;
	if (g < min)
		min = g;
	if (b < min)
		min = b;
	int sum = max + min, delta = max - min, lum = (sum * 240 + 255) / 510,
	    hue = 160, sat = 0;
	if (delta) {
		int divisor = sum <= 255 ? sum : 510 - sum;
		sat = (delta * 240 + divisor / 2) / divisor;
		int rd = ((max - r) * 40 + delta / 2) / delta,
		    gd = ((max - g) * 40 + delta / 2) / delta,
		    bd = ((max - b) * 40 + delta / 2) / delta;
		hue = max == r ? bd - gd :
		      max == g ? 80 + rd - bd :
				 160 + gd - rd;
		if (hue < 0)
			hue += 240;
		if (hue >= 240)
			hue -= 240;
	}
	if (h)
		*h = hue;
	if (l)
		*l = lum;
	if (s)
		*s = sat;
}
static int pe_hls_channel(int low, int high, int hue)
{
	hue = (hue % 240 + 240) % 240;
	if (hue < 40)
		return low + ((high - low) * hue + 20) / 40;
	if (hue < 120)
		return high;
	if (hue < 160)
		return low + ((high - low) * (160 - hue) + 20) / 40;
	return low;
}
__attribute__((ms_abi)) DWORD ColorHLSToRGB(uint16_t hue, uint16_t lum,
					    uint16_t sat)
{
	int l = lum > 240 ? 240 : lum, s = sat > 240 ? 240 : sat;
	if (!s) {
		DWORD v = (l * 255 + 120) / 240;
		return v | (v << 8) | (v << 16);
	}
	int high = l <= 120 ? (l * (240 + s) + 120) / 240 :
			      l + s - (l * s + 120) / 240,
	    low = 2 * l - high;
	int r = (pe_hls_channel(low, high, hue + 80) * 255 + 120) / 240,
	    g = (pe_hls_channel(low, high, hue) * 255 + 120) / 240,
	    b = (pe_hls_channel(low, high, (int)hue - 80) * 255 + 120) / 240;
	return r | (g << 8) | (b << 16);
}
__attribute__((ms_abi)) DWORD ColorAdjustLuma(DWORD color, int amount,
					      BOOL scale)
{
	uint16_t h, l, s;
	ColorRGBToHLS(color, &h, &l, &s);
	int64_t value;
	if (scale) {
		if (amount > 1000)
			amount = 1000;
		if (amount < -1000)
			amount = -1000;
		value = l + (int64_t)(amount < 0 ? l : 240 - l) * amount / 1000;
	} else
		value = (int64_t)amount * 240 / 1000;
	if (value < 0)
		value = 0;
	if (value > 240)
		value = 240;
	return ColorHLSToRGB(h, value, s);
}
