#include "bit_flip_mutator.h"
#include <mutators.h>
#include <afl_helpers.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utils.h>
#include <jansson.h>
#include <jansson_helper.h>

struct bit_flip_state
{
	uint64_t num_bits;

	char * input;
	size_t input_length;
	int iteration;

	mutate_info_t info;
};
typedef struct bit_flip_state bit_flip_state_t;

static int(*const mutate_funcs[])(mutate_info_t *, mutate_buffer_t *) = {
	single_walking_bit,
	two_walking_bit,
	four_walking_bit,
	walking_byte,
	two_walking_byte,
	four_walking_byte,
};

mutator_t arithmetic_mutator = {
	FUNCNAME(create),
	FUNCNAME(cleanup),
	FUNCNAME(mutate),
	FUNCNAME(mutate_extended),
	FUNCNAME(get_state),
	bit_flip_free_state,
	FUNCNAME(set_state),
	FUNCNAME(get_current_iteration),
	FUNCNAME(get_total_iteration_count),
	FUNCNAME(get_input_info),
	FUNCNAME(set_input),
	FUNCNAME(help)
};

/**
 * This function fills in m with all of the function pointers for this mutator.
 * @param m - a pointer to a mutator_t structure
 * @return none
 */
#ifndef ALL_MUTATORS_IN_ONE
BF_MUTATOR_API void init(mutator_t * m)
{
	memcpy(m, &arithmetic_mutator, sizeof(mutator_t));
}
#endif

static bit_flip_state_t * setup_options(char * options)
{
	bit_flip_state_t * state;
	int bits_per_stage[] = { 1, 2, 4, 8, 16, 32 };
	int i;

	state = (bit_flip_state_t *)malloc(sizeof(bit_flip_state_t));
	if (!state)
		return NULL;
	memset(state, 0, sizeof(bit_flip_state_t));

	//Setup defaults
	if (reset_mutate_info(&state->info)) {
		free(state);
		return NULL;
	}

	if (!options || !strlen(options))
		return state;

	PARSE_MUTATE_INFO_OPTIONS(state, options, FUNCNAME(cleanup), 0, 0);
	PARSE_OPTION_INT(state, options, num_bits, "num_bits", FUNCNAME(cleanup));

	if (state->num_bits) {
		state->info.stage = -1;
		for (i = 0; i < ARRAY_SIZE(bits_per_stage) && state->info.stage < 0; i++)
		{
			if (bits_per_stage[i] == state->num_bits)
				state->info.stage = i;
		}
		if (state->info.stage < 0)
		{
			FUNCNAME(cleanup)(state);
			return NULL;
		}
		state->info.one_stage_only = 1;
	}
	return state;
}


/**
 * This function will allocate and initialize the mutator state used in the other Mutator API
 * functions.  
 * @param options - a json string that contains the mutator specific string of options.
 * @param state - Optionally, used to load a previously dumped state (with the get_state()
 * function), that defines the current iteration of the mutator.
 * @param input - used to produce new mutated inputs later when the mutate function is called
 * @param input_length - the size of the input buffer
 * @return a mutator specific structure or NULL on failure.
 */
BF_MUTATOR_API void * FUNCNAME(create)(char * options, char * state, char * input, size_t input_length)
{
	GENERIC_MUTATOR_CREATE(bit_flip_state_t, setup_options, FUNCNAME(cleanup));
}

/**
 * This function will release any resources that the mutator has open 
 * and free the mutator state structure.
 * @param mutator_state - a mutator specific structure previously created by 
 * the create function.  This structure will be freed and should not be referenced afterwards.
 */
BF_MUTATOR_API void FUNCNAME(cleanup)(void * mutator_state)
{
	cleanup_mutate_info(&((bit_flip_state_t *)mutator_state)->info);
	GENERIC_MUTATOR_CLEANUP(bit_flip_state_t)
}

static int mutate_inner(void * mutator_state, char * buffer, size_t buffer_length, int is_thread_safe)
{
	bit_flip_state_t * state = (bit_flip_state_t *)mutator_state;
	mutate_buffer_t buf;
	int ret;
	if (buffer_length < state->input_length)
		return -1;

	buf.buffer = (uint8_t *)buffer;
	buf.length = MIN(buffer_length, state->input_length);
	buf.max_length = buffer_length;
	memcpy(buf.buffer, state->input, buf.length);

	if(is_thread_safe && take_mutex(state->info.mutate_mutex))
		return -1;
	state->iteration++;
	ret = mutate_one(&state->info, &buf, mutate_funcs, ARRAY_SIZE(mutate_funcs));
	if (is_thread_safe && release_mutex(state->info.mutate_mutex))
		return -1;
	return ret;
}

/**
 * This function will mutate the input given in the create function and return it in the buffer argument.
 * @param mutator_state - a mutator specific structure previously created by the create function.
 * @param buffer - a buffer that the mutated input will be written to
 * @param buffer_length - the size of the passed in buffer argument.  It must be at least as large as
 * the original input buffer.
 * @return - the length of the mutated data, 0 when the mutator is out of mutations, or -1 on error
 */
BF_MUTATOR_API int FUNCNAME(mutate)(void * mutator_state, char * buffer, size_t buffer_length)
{
	return mutate_inner(mutator_state, buffer, buffer_length, 0);
}

/**
 * This function will mutate the input given in the create function and return it in the buffer argument.
 * This function also accepts a set of flags which instruct it how to mutate the input.  See global_types.h
 * for the list of available flags.
 * @param mutator_state - a mutator specific structure previously created by the create function.
 * @param buffer - a buffer that the mutated input will be written to
 * @param buffer_length - the size of the passed in buffer argument.  It must be at least as large as
 * the original input buffer.
 * @param flags - A set of mutate flags that modify how this mutator mutates the input.
 * @return - the length of the mutated data, 0 when the mutator is out of mutations, or -1 on error
 */
BF_MUTATOR_API int FUNCNAME(mutate_extended)(void * mutator_state, char * buffer, size_t buffer_length, uint64_t flags)
{
	SINGLE_INPUT_MUTATE_EXTENDED(bit_flip_state_t, state->info.mutate_mutex);
}

/**
 * This function will return the state of the mutator.  The returned value can be used to restart the
 * mutator at a later time, by passing it to the create or set_state function.  It is the caller's
 * responsibility to free the memory allocated here by calling the free_state function.
 * @param mutator_state - a mutator specific structure previously created by the create function.
 * @return - a buffer that defines the current state of the mutator.
 */
BF_MUTATOR_API char * FUNCNAME(get_state)(void * mutator_state)
{
	bit_flip_state_t * state = (bit_flip_state_t *)mutator_state;
	json_t *state_obj, *temp;
	char * ret;

	state_obj = json_object();
	ADD_INT(temp, state->iteration, state_obj, "iteration");
	ADD_INT(temp, state->num_bits, state_obj, "num_bits");
	if (!add_mutate_info_to_json(state_obj, &state->info))
		return NULL;

	ret = json_dumps(state_obj, 0);
	json_decref(state_obj);
	return ret;
}

/**
 * This function will set the current state of the mutator.
 * This can be used to restart a mutator once from a previous run.
 * @param mutator_state - a mutator specific structure previously created by the create function.
 * @param state - a previously dumped state buffer obtained by the get_state function.
 * @return 0 on success or non-zero on failure
 */
BF_MUTATOR_API int FUNCNAME(set_state)(void * mutator_state, char * state)
{
	bit_flip_state_t * current_state = (bit_flip_state_t *)mutator_state;
	int result, temp_int;
	if (!state)
		return 1;
	GET_INT(temp_int, state, current_state->iteration, "iteration", result);
	GET_INT(temp_int, state, current_state->num_bits, "num_bits", result);
	if (get_mutate_info_from_json(state, &current_state->info))
		return 1;
	return 0;
}

/**
 * This function will return the current iteration count of the mutator, i.e.
 * how many mutations have been generated with it.
 * @param mutator_state - a mutator specific structure previously created by the create function.
 * @return value - the number of previously generated mutations
 */
BF_MUTATOR_API int FUNCNAME(get_current_iteration)(void * mutator_state)
{
	GENERIC_MUTATOR_GET_ITERATION(bit_flip_state_t);
}

/**
 * Returns the total number of mutations possible with this mutator and the current options.
 * @param mutator_state - a mutator specific structure previously created by the create function.
 * @return the number of possible mutations with this mutator, or -1 if infinite or the number
 * cannot be determined.
 */
BF_MUTATOR_API int FUNCNAME(get_total_iteration_count)(void * mutator_state)
{
	bit_flip_state_t * state = (bit_flip_state_t *)mutator_state;
	if (state->info.one_stage_only)
	{
		if(state->num_bits < 8) //1, 2, 4
			return (state->input_length << 3) - (state->num_bits - 1);
		return state->input_length - ((state->num_bits >> 3) - 1); //8, 16, 32
	}
	//Add up all rounds
	return ((state->input_length << 3) //1
		+ ((state->input_length << 3) - 1) //2
		+ ((state->input_length << 3) - 3) //4
		+ state->input_length //8
		+ (state->input_length - 1) //16
		+ (state->input_length - 3)); //32
}

/**
 * Obtains information about the inputs that were given to the mutator when it was created
 * @param mutator_state - a mutator specific structure previously created by the create function.
 * @param num_inputs - a pointer to an integer used to return the number of inputs given to this mutator
 * when it was created.  This parameter is optional and can be NULL, if this information is not needed
 * @param input_sizes - a pointer to a size_t array used to return the sizes of the inputs given to this
 * mutator when it was created. This parameter is optional and can be NULL, if this information is not needed.
 */
BF_MUTATOR_API void FUNCNAME(get_input_info)(void * mutator_state, int * num_inputs, size_t **input_sizes)
{
	SINGLE_INPUT_GET_INFO(bit_flip_state_t);
}

/**
 * This function will set the mutator's input to something new.
 * @param mutator_state - a mutator specific structure previously created by the create function.
 * @param new_input - The new input used to produce new mutated inputs later when the mutate function is called
 * @param input_length - the size in bytes of the input buffer.
 * @return 0 on success and -1 on failure
 */
BF_MUTATOR_API int FUNCNAME(set_input)(void * mutator_state, char * new_input, size_t input_length)
{
	GENERIC_MUTATOR_SET_INPUT(bit_flip_state_t);
}

/**
 * This function sets a help message for the mutator.
 * @param help_str - A pointer that will be updated to point to the new help string.
 * @return 0 on success and -1 on failure
 */
BF_MUTATOR_API int FUNCNAME(help)(char **help_str)
{
	GENERIC_MUTATOR_HELP(
"bit_flip - afl-based bit flip mutator\n"
"Options:\n"
"  num_bits              The number of bits to operate on; either 1, 2, 4, 8,\n"
"                          16, or 32. The default option is to do all six of\n"
"                          the options, one after another.\n"
"\n"
	);
}
