#pragma once

/*
 * The other half of a `key=value` record: the comma-separated value, read as a list of typed
 * destinations rather than as a printf format string beside a hand-counted field total.
 *
 * keys.h spells a line's key once, in a table both halves of the format share. This spells a
 * line's *value* once, in the same spirit. A loader case used to be a block of `unsigned int`
 * scratch variables, an sscanf whose format had to agree with them, a literal count that had to
 * agree with both, and a run of `(bool)(x != 0U)` casts copying the scratch into the record.
 * Three of those four could drift apart without the compiler noticing, and one of them - the
 * count - is the thing a format's compatibility rules turn on.
 *
 * Here the field list *is* the format, the destination's type picks the conversion, and the
 * total comes off the array.
 *
 * Stricter than the sscanf() it replaced, deliberately, exactly as inkstand_key_lookup() is
 * stricter than the one *it* replaced. A record file is text on a disk a user can edit, so it is
 * an ingress like a network is:
 *
 *   - A number too wide for its destination fails rather than wrapping or truncating. `%u`
 *     into an `unsigned int` and a cast down to `uint8_t` turned a percentage of 300 into 44; a
 *     value that does not fit now drops the record instead of dressing up as a plausible
 *     reading. This is also what retires the widest-scan-then-bound dance for a signed 32-bit
 *     field: `%d` on a number past INT32_MAX is undefined, and glibc's answer is 0 - which, for
 *     a coordinate, is a place on the map. An out-of-range number is now simply a field that
 *     did not read.
 *   - A negative number in an unsigned field fails rather than wrapping. glibc's `%u` reads
 *     "-1" as 4294967295.
 *   - A token with trailing rubbish fails rather than yielding its leading digits.
 *
 * None of that can reject a line a well-behaved writer wrote: every writer hands the value a
 * number already inside the destination's range.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What a token is converted into. One per destination type a record actually stores; the
   macro below picks the right one, so nothing outside this header names these. */
enum inkstand_field_type {
    INKSTAND_FIELD_TYPE_U32 = 0,
    INKSTAND_FIELD_TYPE_U16,
    INKSTAND_FIELD_TYPE_U8,
    INKSTAND_FIELD_TYPE_I32,
    INKSTAND_FIELD_TYPE_I16,
    INKSTAND_FIELD_TYPE_BOOL,
    INKSTAND_FIELD_TYPE_F32,
};

/* One field of a value: where it goes, and how to get it there. */
struct inkstand_field {
    enum inkstand_field_type type;
    void *out;
};

/*
 * A field, named by its destination.
 *
 * `INKSTAND_FIELD(&reading.percent)` is a `uint8_t` field because `percent` is a `uint8_t`, and
 * there is no second place to keep that in step. A destination whose type a record does not
 * store is a compile error naming this line rather than a silent write of the wrong width
 * through a `void *` - which is the whole reason the type is not written out at the call site.
 *
 * A macro rather than the small static helper the style guide asks for, for the reason
 * INKWELL_ARRAY_LEN is one: the type differs at every call site, and C17 has no other way to
 * dispatch on it. The associations are the exact-width typedefs, so a target where one of them
 * is spelled differently - `uint32_t` as `unsigned long` - fails to compile here rather than
 * anywhere subtler.
 */
#define INKSTAND_FIELD(ptr)                                                                        \
    ((struct inkstand_field){_Generic((ptr),                                                       \
                             uint32_t *: INKSTAND_FIELD_TYPE_U32,                                  \
                             uint16_t *: INKSTAND_FIELD_TYPE_U16,                                  \
                             uint8_t *: INKSTAND_FIELD_TYPE_U8,                                    \
                             int32_t *: INKSTAND_FIELD_TYPE_I32,                                   \
                             int16_t *: INKSTAND_FIELD_TYPE_I16,                                   \
                             bool *: INKSTAND_FIELD_TYPE_BOOL,                                     \
                             float *: INKSTAND_FIELD_TYPE_F32),                                    \
                             (ptr)})

/*
 * Read a comma-separated value into `count` fields, and say how many arrived.
 *
 * Fields are filled left to right and the walk stops at the first token that is missing or
 * does not convert, so the return is the number of *leading* fields that read - which is what
 * sscanf's return meant, and what the format's compatibility rules are already written
 * against. A caller that needs the whole line compares against the array length; one reading a
 * line that grew a field on the end compares against the length it had before. Fields past the
 * return are left exactly as the caller set them, which is how an absent trailing field keeps
 * its default.
 *
 * Extra tokens past the last field are ignored, as sscanf ignored them.
 */
size_t inkstand_fields_read(const char *value, const struct inkstand_field *fields, size_t count);

#ifdef __cplusplus
}
#endif
