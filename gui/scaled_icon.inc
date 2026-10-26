/* commctrl.h LoadIconWithScaleDown: HRESULT + an output HICON, not HIMAGELIST.
 * Existing resource decoding supplies real DIB pixels; unsupported encodings fail.
 * Resampling is nearest-neighbor; a higher-quality filter remains future work. */
static int32_t pe_icon_failure(DWORD error)
{
	return error ? (int32_t)(0x80070000u | (error & 0xffffu)) :
		       (int32_t)0x80004005u;
}
__attribute__((ms_abi)) int32_t LoadIconWithScaleDown(HANDLE instance,
						      const uint16_t *name,
						      int width, int height,
						      HANDLE *out)
{
	if (!out) {
		SetLastError(87);
		return (int32_t)0x80004003u;
	}
	*out = NULL;
	if (!name || width < 1 || height < 1 || width > 4096 || height > 4096) {
		SetLastError(87);
		return (int32_t)0x80070057u;
	}
	HANDLE source = LoadImageW(instance, name, 1, 0, 0, 0);
	if (!source)
		return pe_icon_failure(GetLastError());
	HANDLE result = NULL;
	DWORD error = 0;
	{
		PE_STATE_GUARD;
		struct pe_gui_object *image =
			pe_gui_lookup(source, PE_GUI_ICON);
		struct pe_raster *src = image ? pe_raster_resource(image) :
						NULL;
		if (src) {
			struct pe_raster *scaled =
				pe_raster_new(width, height, true);
			if (scaled) {
				for (int y = 0; y < height; y++)
					for (int x = 0; x < width; x++)
						scaled->pixels[(size_t)y * width +
							       x] =
							pe_raster_get(
								src,
								(int64_t)x *
									src->width /
									width,
								(int64_t)y *
									src->height /
									height);
				struct pe_gui_object *icon =
					pe_gui_new(PE_GUI_ICON);
				if (icon) {
					icon->raster = scaled;
					icon->image.width = width;
					icon->image.height = height;
					result = pe_gui_handle(icon);
				} else
					pe_raster_free(&(struct pe_gui_object){
						.raster = scaled });
			}
		}
		if (!result)
			error = GetLastError();
	}
	DestroyIcon(source);
	if (!result) {
		SetLastError(error);
		return pe_icon_failure(error);
	}
	*out = result;
	return 0;
}
