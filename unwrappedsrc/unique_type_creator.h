#include "types.h"

//if the model is in the heap, can_reuse_parameter = true.
//otherwise, if it has limited lifetime (for example, it's on the stack), can_reuse_parameter = false, because we need to create a new model in the heap before making it unique
Type* get_unique_type(Type* model, bool can_reuse_parameter);
inline Type* get_non_convec_unique_type(Type model) {return get_unique_type(&model, false);}

//call this to test the uniqueing function.
void test_unique_types();