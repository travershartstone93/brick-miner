// Hand-written direct bindings used only as a comparison after discovery and qualification.
// Compile in the original source translation unit, with one comparison selected per build.
#if defined(CHECK_RADIX_VALUE)
inline int reference_binding(unsigned char value) { return chvalue(value); }
#elif defined(CHECK_HEX_DIGIT)
inline unsigned char reference_binding(unsigned int value) { return xdigit(value); }
#elif defined(CHECK_NUMBER_PREFIX)
inline bool reference_binding(const char* value) { return is_number(value); }
#endif
