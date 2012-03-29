#include "ccv.h"
#include "case.h"
#include "ccv_case.h"

/* numeric tests are more like functional tests rather than unit tests:
 * the following tests contain:
 * 1. minimization of the famous rosenbrock function;
 * 2. compute ssd with ccv_convolve, and compare the result with naive method
 * 3. compare the result from ccv_distance_transform (linear time) with reference implementation from voc-release4 (O(nlog(n))) */

int rosenbrock(const ccv_dense_matrix_t* x, double* f, ccv_dense_matrix_t* df, void* data)
{
	int* steps = (int*)data;
	(*steps)++;
	int i;
	double rf = 0;
	double* x_vec = x->data.f64;
	for (i = 0; i < 1; i++)
		rf += 100 * (x_vec[i + 1] - x_vec[i] * x_vec[i]) * (x_vec[i + 1] - x_vec[i] * x_vec[i]) + (1 - x_vec[i]) * (1 - x_vec[i]);
	*f = rf;
	double* df_vec = df->data.f64;
	ccv_zero(df);
	df_vec[0] = df_vec[1] = 0;
	for (i = 0; i < 1; i++)
		df_vec[i] = -400 * x_vec[i] * (x_vec[i+1] - x_vec[i] * x_vec[i]) - 2 * (1 - x_vec[i]);
	for (i = 1; i < 2; i++)
		df_vec[i] += 200 * (x_vec[i] - x_vec[i - 1] * x_vec[i - 1]);
	return 0;
}

TEST_CASE("minimize rosenbrock")
{
	ccv_dense_matrix_t* x = ccv_dense_matrix_new(1, 2, CCV_64F | CCV_C1, 0, 0);
	ccv_zero(x);
	int steps = 0;
	ccv_minimize_param_t params;
	params.interp = 0.1;
	params.extrap = 3.0;
	params.max_iter = 20;
	params.ratio = 10.0;
	params.sig = 0.1;
	params.rho = 0.05;
	ccv_minimize(x, 25, 1.0, rosenbrock, params, &steps);
	double dx[2] = { 1, 1 };
	REQUIRE_ARRAY_EQ_WITH_TOLERANCE(double, x->data.f64, dx, 2, 1e-6, "the global minimal should be at (1.0, 1.0)");
	ccv_matrix_free(x);
}

#include "ccv_internal.h"

static void naive_ssd(ccv_dense_matrix_t* image, ccv_dense_matrix_t* template, ccv_dense_matrix_t* out)
{
	int thw = template->cols / 2;
	int thh = template->rows / 2;
	int i, j, k, x, y, ch = CCV_GET_CHANNEL(image->type);
	unsigned char* i_ptr = image->data.u8 + thh * image->step;
	double* o = out->data.f64 + out->cols * thh;
	ccv_zero(out);
	for (i = thh; i < image->rows - thh - 1; i++)
	{
		for (j = thw; j < image->cols - thw - 1; j++)
		{
			unsigned char* t_ptr = template->data.u8;
			unsigned char* j_ptr = i_ptr - thh * image->step;
			o[j] = 0;
			for (y = -thh; y <= thh; y++)
			{
				for (x = -thw; x <= thw; x++)
					for (k = 0; k < ch; k++)
						o[j] += (j_ptr[(x + j) * ch + k] - t_ptr[(x + thw) * ch + k]) * (j_ptr[(x + j) * ch + k] - t_ptr[(x + thw) * ch + k]);
				t_ptr += template->step;
				j_ptr += image->step;
			}
		}
		i_ptr += image->step;
		o += out->cols;
	}
}

TEST_CASE("convolution ssd (sum of squared differences) v.s. naive ssd")
{
	ccv_dense_matrix_t* street = 0;
	ccv_dense_matrix_t* pedestrian = 0;
	ccv_read("../samples/pedestrian.png", &pedestrian, CCV_IO_ANY_FILE);
	ccv_read("../samples/street.png", &street, CCV_IO_ANY_FILE);
	ccv_dense_matrix_t* result = 0;
	ccv_convolve(street, pedestrian, &result, CCV_64F, 0);
	ccv_dense_matrix_t* square = 0;
	ccv_multiply(street, street, (ccv_matrix_t**)&square, 0);
	ccv_dense_matrix_t* sat = 0;
	ccv_sat(square, &sat, 0, CCV_PADDING_ZERO);
	ccv_matrix_free(square);
	double sum[] = {0, 0, 0};
	int i, j, k;
	int ch = CCV_GET_CHANNEL(street->type);
	unsigned char* p_ptr = pedestrian->data.u8;
#define for_block(_, _for_get) \
	for (i = 0; i < pedestrian->rows; i++) \
	{ \
		for (j = 0; j < pedestrian->cols; j++) \
			for (k = 0; k < ch; k++) \
				sum[k] += _for_get(p_ptr, j * ch + k, 0) * _for_get(p_ptr, j * ch + k, 0); \
		p_ptr += pedestrian->step; \
	}
	ccv_matrix_getter(pedestrian->type, for_block);
#undef for_block
	int phw = pedestrian->cols / 2;
	int phh = pedestrian->rows / 2;
	ccv_dense_matrix_t* output = ccv_dense_matrix_new(street->rows, street->cols, CCV_64F | CCV_C1, 0, 0);
	ccv_zero(output);
	unsigned char* s_ptr = sat->data.u8 + sat->step * phh;
	unsigned char* r_ptr = result->data.u8 + result->step * phh;
	double* o_ptr = output->data.f64 + output->cols * phh;
#define for_block(_for_get_s, _for_get_r) \
	for (i = phh; i < output->rows - phh - 1; i++) \
	{ \
		for (j = phw; j < output->cols - phw - 1; j++) \
		{ \
			o_ptr[j] = 0; \
			for (k = 0; k < ch; k++) \
			{ \
				o_ptr[j] += (_for_get_s(s_ptr + sat->step * ccv_min(phh + 1, sat->rows - i - 1), ccv_min(j + phw + 1, sat->cols - 1) * ch + k, 0) \
						  - _for_get_s(s_ptr + sat->step * ccv_min(phh + 1, sat->rows - i - 1), ccv_max(j - phw, 0) * ch + k, 0) \
						  + _for_get_s(s_ptr + sat->step * ccv_max(-phh, -i), ccv_max(j - phw, 0) * ch + k, 0) \
						  - _for_get_s(s_ptr + sat->step * ccv_max(-phh, -i), ccv_min(j + phw + 1, sat->cols - 1) * ch + k, 0)) \
						  + sum[k] - 2.0 * _for_get_r(r_ptr, j * ch + k, 0); \
			} \
		} \
		s_ptr += sat->step; \
		r_ptr += result->step; \
		o_ptr += output->cols; \
	}
	ccv_matrix_getter(sat->type, ccv_matrix_getter_a, result->type, for_block);
#undef for_block
	ccv_matrix_free(result);
	ccv_matrix_free(sat);
	ccv_dense_matrix_t* final = 0;
	ccv_slice(output, (ccv_matrix_t**)&final, 0, phh, phw, output->rows - phh * 2, output->cols - phw * 2);
	ccv_zero(output);
	naive_ssd(street, pedestrian, output);
	ccv_dense_matrix_t* ref = 0;
	ccv_slice(output, (ccv_matrix_t**)&ref, 0, phh, phw, output->rows - phh * 2, output->cols - phw * 2);
	ccv_matrix_free(output);
	ccv_matrix_free(pedestrian);
	ccv_matrix_free(street);
	REQUIRE_MATRIX_EQ(ref, final, "ssd computed by convolution doesn't match the one computed by naive method");
	ccv_matrix_free(final);
	ccv_matrix_free(ref);
}

// divide & conquer method for distance transform (copied directly from dpm-matlab (voc-release4)

static inline int square(int x) { return x*x; }

// dt helper function
void dt_helper(double *src, double *dst, int *ptr, int step, 
	       int s1, int s2, int d1, int d2, double a, double b) {
 if (d2 >= d1) {
   int d = (d1+d2) >> 1;
   int s = s1;
   for (int p = s1+1; p <= s2; p++)
     if (src[s*step] + a*square(d-s) + b*(d-s) > 
	 src[p*step] + a*square(d-p) + b*(d-p))
	s = p;
   dst[d*step] = src[s*step] + a*square(d-s) + b*(d-s);
   ptr[d*step] = s;
   dt_helper(src, dst, ptr, step, s1, s, d1, d-1, a, b);
   dt_helper(src, dst, ptr, step, s, s2, d+1, d2, a, b);
 }
}

// dt of 1d array
void dt1d(double *src, double *dst, int *ptr, int step, int n, 
	  double a, double b) {
  dt_helper(src, dst, ptr, step, 0, n-1, 0, n-1, a, b);
}

void daq_distance_transform(ccv_dense_matrix_t* a, ccv_dense_matrix_t** b, double dx, double dy, double dxx, double dyy)
{
	ccv_dense_matrix_t* dc = ccv_dense_matrix_new(a->rows, a->cols, CCV_64F | CCV_C1, 0, 0);
	ccv_dense_matrix_t* db = *b = ccv_dense_matrix_new(a->rows, a->cols, CCV_64F | CCV_C1, 0, 0);
	unsigned char* a_ptr = a->data.u8;
	double* b_ptr = db->data.f64;
	int i, j;
#define for_block(_, _for_get) \
	for (i = 0; i < a->rows; i++) \
	{ \
		for (j = 0; j < a->cols; j++) \
			b_ptr[j] = _for_get(a_ptr, j, 0); \
		b_ptr += db->cols; \
		a_ptr += a->step; \
	}
	ccv_matrix_getter(a->type, for_block);
#undef for_block
	int* ix = (int*)calloc(a->cols * a->rows, sizeof(int));
	int* iy = (int*)calloc(a->cols * a->rows, sizeof(int));
	b_ptr = db->data.f64;
	double* c_ptr = dc->data.f64;
	for (i = 0; i < a->rows; i++)
		dt1d(b_ptr + i * a->cols, c_ptr + i * a->cols, ix + i * a->cols, 1, a->cols, dxx, dx);
	for (j = 0; j < a->cols; j++)
		dt1d(c_ptr + j, b_ptr + j, iy + j, a->cols, a->rows, dyy, dy);
	free(ix);
	free(iy);
	ccv_matrix_free(dc);
}

TEST_CASE("ccv_distance_transform (linear time) v.s. distance transform using divide & conquer (O(nlog(n)))")
{
	ccv_dense_matrix_t* geometry = 0;
	ccv_read("../samples/geometry.png", &geometry, CCV_IO_GRAY | CCV_IO_ANY_FILE);
	ccv_dense_matrix_t* distance = 0;
	ccv_distance_transform(geometry, &distance, 0, 1, 1, 0.1, 0.1, CCV_GSEDT);
	ccv_dense_matrix_t* ref = 0;
	daq_distance_transform(geometry, &ref, 1, 1, 0.1, 0.1);
	ccv_matrix_free(geometry);
	REQUIRE_MATRIX_EQ(distance, ref, "distance transform computed by ccv_distance_transform doesn't match the one computed by divide & conquer (voc-release4)");
	ccv_matrix_free(ref);
	ccv_matrix_free(distance);
}

#include "case_main.h"