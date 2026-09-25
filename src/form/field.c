/*
 * A form's rows, read through the application's table - see inkstand/form/field.h.
 */

#include "inkstand/form/field.h"

const struct inkstand_form_field *inkstand_form_field(const struct inkstand_form *form,
                                                      uint16_t id) {
    /* Row 0 answers for every id the table does not have, which is what makes every accessor
       below safe to call with whatever a caller is holding. */
    if (id >= form->count) {
        id = 0U;
    }
    /* Through void: the stride is a whole row of the application's struct, so the address is
       as aligned as the table itself, and the byte pointer is only the arithmetic. */
    const void *row = (const unsigned char *)form->fields + (size_t)id * form->stride;
    return (const struct inkstand_form_field *)row;
}

bool inkstand_form_section_has_fields(const struct inkstand_form *form, uint16_t section) {
    for (uint16_t id = 1U; id < form->count; ++id) {
        if (inkstand_form_field(form, id)->section == section) {
            return true;
        }
    }
    return false;
}

uint32_t inkstand_form_bit(const struct inkstand_form *form, uint16_t id) {
    const struct inkstand_form_field *field = inkstand_form_field(form, id);
    return field->kind == INKSTAND_FORM_FLAG ? field->limit : 0U;
}

uint32_t inkstand_form_enum_count(const struct inkstand_form *form, uint16_t id) {
    const struct inkstand_form_field *field = inkstand_form_field(form, id);
    return field->kind == INKSTAND_FORM_ENUM ? field->limit : 0U;
}

const char *inkstand_form_enum_name(const struct inkstand_form *form, uint16_t id, uint32_t value) {
    const struct inkstand_form_field *field = inkstand_form_field(form, id);
    const char *name = NULL;
    if (field->kind == INKSTAND_FORM_ENUM && field->enum_name != NULL) {
        name = field->enum_name(value);
    }
    /* The callback is the application's and may have no name for a value - one a newer peer
       sent, or one past the end of its own list. The promise here is never NULL, so a caller
       drawing the row never has to ask. */
    return name != NULL ? name : inkcell_str(INKCELL_STR_COMMON_UNKNOWN_SHORT);
}

/*
 * The field's own limit, unclamped. An edit buffer is best measured from these limits rather
 * than the other way round: clamped to a buffer, a field wider than it would be offered a
 * shorter keyboard cap and nothing anywhere would say the value had been cut.
 */
uint32_t inkstand_form_text_max(const struct inkstand_form *form, uint16_t id) {
    const struct inkstand_form_field *field = inkstand_form_field(form, id);
    if (field->kind != INKSTAND_FORM_TEXT && field->kind != INKSTAND_FORM_KEY) {
        return 0U;
    }
    return field->limit;
}

uint32_t inkstand_form_key_choices(const struct inkstand_form *form, uint16_t id) {
    const struct inkstand_form_field *field = inkstand_form_field(form, id);
    return field->kind == INKSTAND_FORM_KEY ? field->choices : 0U;
}

uint32_t inkstand_form_number_step(const struct inkstand_form *form, uint16_t id, uint32_t value,
                                   int delta) {
    const struct inkstand_form_field *field = inkstand_form_field(form, id);
    if (field->kind != INKSTAND_FORM_NUMBER) {
        return value;
    }
    return inkstand_form_presets_step(&field->presets, value, delta);
}

bool inkstand_form_number_track(const struct inkstand_form *form, uint16_t id, uint32_t value,
                                struct inkstand_form_track *out) {
    const struct inkstand_form_field *field = inkstand_form_field(form, id);
    if (field->kind != INKSTAND_FORM_NUMBER) {
        return false;
    }
    return inkstand_form_presets_track(&field->presets, value, out);
}
