//What is the "pointer to object"?
//Objects are not standard!
//So what does it mean?
//Pointer to a MSVC object? To a GCC object? To a Borland C object?
//Having a "pointer to object" in the plug-in API was an awful decision.
//It forces plug-in autors use MSVC (for GPL projects, it makes me facepalm).
//My workaround was really dirty and took lots of time to dig into low level (I've had to sacrifice important things such as relocable structures).

typedef void* AwfulDecision;

struct _VFBitmap
{
	AwfulDecision VirtualMethods;	//Took a whole day with debugger to figure out it's a void*.
	Pixel *         data;
	Pixel *         palette;
	int            depth;
	PixCoord      w, h;
	PixOffset      pitch;
	PixOffset      modulo;
	PixOffset      size;
	PixOffset      offset;
};

struct _FilterFunctions
{
	int ToDo;
};

struct _FilterActivation
{
	FilterDefinition *filter;
	void *filter_data;
	_VFBitmap *dst, *src;
	_VFBitmap *__reserved0, *const last;
	unsigned long x1, y1, x2, y2;
};