
enum ServiceNameError{
	SUCCESS,
	NAME_TOO_LONG,
	NAME_EXISTING
};

enum ServiceNameError registerService(const char *name);
