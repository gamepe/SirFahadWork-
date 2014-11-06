#include "jpge.h"
#include "jpgd.h"
#include "stb_image.c"
#include "timer.h"
#include <ctype.h>

#if defined(_MSC_VER)
  #define strcasecmp _stricmp
#else
  #define strcpy_s(d, c, s) strcpy(d, s)
#endif

static int print_usage()
{
  printf("/n/n/nUsage: jpge option sourcefile destinationfile\n");

  printf("\nexample:\n");
  printf("test compression: jpge original.png compressed.jpg\n");
  printf("test decompression: jpge -d compressed.jpg uncompressed.tga\n");
  printf("exhaustively test compressor: jpge -x orig.png\n");
  
  return EXIT_FAILURE;
}

static char s_log_filename[256];

static void log_printf(const char *pMsg, ...)
{
  va_list args;

  va_start(args, pMsg);
  char buf[2048];
  vsnprintf(buf, sizeof(buf) - 1, pMsg, args);
  buf[sizeof(buf) - 1] = '\0';
  va_end(args);

  printf("%s", buf);

  if (s_log_filename[0])
  {
    FILE *pFile = fopen(s_log_filename, "a+");
    if (pFile)
    {
      fprintf(pFile, "%s", buf);
      fclose(pFile);
    }
  }
}

static uint get_file_size(const char *pFilename)
{
  FILE *pFile = fopen(pFilename, "rb");
  if (!pFile) return 0;
  fseek(pFile, 0, SEEK_END);
  uint file_size = ftell(pFile);
  fclose(pFile);
  return file_size;
}

struct image_compare_results
{
  image_compare_results() { memset(this, 0, sizeof(*this)); }

  double max_err;
  double mean;
  double mean_squared;
  double root_mean_squared;
  double peak_snr;
};

static void get_pixel(int* pDst, const uint8 *pSrc, bool luma_only, int num_comps)
{
  int r, g, b;
  if (num_comps == 1)
  {
    r = g = b = pSrc[0];
  }
  else if (luma_only)
  {
    const int YR = 19595, YG = 38470, YB = 7471;
    r = g = b = (pSrc[0] * YR + pSrc[1] * YG + pSrc[2] * YB + 32768) / 65536;
  }
  else
  {
    r = pSrc[0]; g = pSrc[1]; b = pSrc[2];
  }
  pDst[0] = r; pDst[1] = g; pDst[2] = b;
}


static void image_compare(image_compare_results &results, int width, int height, const uint8 *pComp_image, int comp_image_comps, const uint8 *pUncomp_image_data, int uncomp_comps, bool luma_only)
{
  double hist[256];
  memset(hist, 0, sizeof(hist));

  const uint first_channel = 0, num_channels = 3;
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      int a[3]; get_pixel(a, pComp_image + (y * width + x) * comp_image_comps, luma_only, comp_image_comps);
      int b[3]; get_pixel(b, pUncomp_image_data + (y * width + x) * uncomp_comps, luma_only, uncomp_comps);
      for (uint c = 0; c < num_channels; c++)
        hist[labs(a[first_channel + c] - b[first_channel + c])]++;
    }
  }

  results.max_err = 0;
  double sum = 0.0f, sum2 = 0.0f;
  for (uint i = 0; i < 256; i++)
  {
    if (!hist[i])
      continue;
    if (i > results.max_err)
      results.max_err = i;
    double x = i * hist[i];
    sum += x;
    sum2 += i * x;
  }

  double total_values = width * height;

  results.mean = sum / total_values;
  results.mean_squared = sum2 / total_values;

  results.root_mean_squared = sqrt(results.mean_squared);

  if (!results.root_mean_squared)
    results.peak_snr = 1e+10f;
  else
    results.peak_snr = log10(255.0f / results.root_mean_squared) * 20.0f;
}

static int exhausive_compression_test(const char *pSrc_filename, bool use_jpgd)
{
  int status = EXIT_SUCCESS;

  const int req_comps = 3; 
  int width = 0, height = 0, actual_comps = 0;
  uint8 *pImage_data = stbi_load(pSrc_filename, &width, &height, &actual_comps, req_comps);
  if (!pImage_data)
  {
    log_printf("failed loading file \"%s\"!\n", pSrc_filename);
    return EXIT_FAILURE;
  }


  int orig_buf_size = width * height * 3; 
  if (orig_buf_size < 1024) orig_buf_size = 1024;
  void *pBuf = malloc(orig_buf_size);

  uint8 *pUncomp_image_data = NULL;

  double max_err = 0;
  double lowest_psnr = 9e+9;
  double threshold_psnr = 9e+9;
  double threshold_max_err = 0.0f;

  image_compare_results prev_results;

  for (uint quality_factor = 1; quality_factor <= 100; quality_factor++)
  {
    for (uint subsampling = 0; subsampling <= jpge::H2V2; subsampling++)
    {
      for (uint optimize_huffman_tables = 0; optimize_huffman_tables <= 1; optimize_huffman_tables++)
      {
      
        jpge::params params;
        params.m_quality = quality_factor;
        params.m_subsampling = static_cast<jpge::subsampling_t>(subsampling);
        params.m_two_pass_flag = (optimize_huffman_tables != 0);

        int comp_size = orig_buf_size;
        if (!jpge::compress_image_to_jpeg_file_in_memory(pBuf, comp_size, width, height, req_comps, pImage_data, params))
        {
          status = EXIT_FAILURE;
          goto failure;
        }

        int uncomp_width = 0, uncomp_height = 0, uncomp_actual_comps = 0, uncomp_req_comps = 3;
        free(pUncomp_image_data);
        if (use_jpgd)
          pUncomp_image_data = jpgd::decompress_jpeg_image_from_memory((const stbi_uc*)pBuf, comp_size, &uncomp_width, &uncomp_height, &uncomp_actual_comps, uncomp_req_comps);
        else
          pUncomp_image_data = stbi_load_from_memory((const stbi_uc*)pBuf, comp_size, &uncomp_width, &uncomp_height, &uncomp_actual_comps, uncomp_req_comps);
        if (!pUncomp_image_data)
        {
          status = EXIT_FAILURE;
          goto failure;
        }

        if ((uncomp_width != width) || (uncomp_height != height))
        {
          status = EXIT_FAILURE;
          goto failure;
        }

        image_compare_results results;
        image_compare(results, width, height, pImage_data, req_comps, pUncomp_image_data, uncomp_req_comps, (params.m_subsampling == jpge::Y_ONLY) || (actual_comps == 1) || (uncomp_actual_comps == 1));
        if (results.max_err > max_err) max_err = results.max_err;
        if (results.peak_snr < lowest_psnr) lowest_psnr = results.peak_snr;

        if (quality_factor == 1)
        {
          if (results.peak_snr < threshold_psnr)
            threshold_psnr = results.peak_snr;
          if (results.max_err > threshold_max_err)
            threshold_max_err = results.max_err;
        }
        else
        {
          
          if ((results.peak_snr < (threshold_psnr - 3.0f)) || (results.peak_snr < 6.0f)) 
          {
            status = EXIT_FAILURE;
            goto failure;
          }
          if (optimize_huffman_tables)
          {
            if ((prev_results.max_err != results.max_err) || (prev_results.peak_snr != results.peak_snr))
            {
              status = EXIT_FAILURE;
              goto failure;
            }
          }
        }

        prev_results = results;
      }
    }
  }


failure:
  free(pImage_data);
  free(pBuf);
  free(pUncomp_image_data);

  return status;
}

static int test_jpgd(const char *pSrc_filename, const char *pDst_filename)
{
  
  const int req_comps = 3;
  int width = 0, height = 0, actual_comps = 0;
  
  timer tm;
  tm.start();

  uint8 *pImage_data = jpgd::decompress_jpeg_image_from_file(pSrc_filename, &width, &height, &actual_comps, req_comps);

  tm.stop();

  if (!pImage_data)
  {
    log_printf("failed loading JPEG file \"%s\"!\n", pSrc_filename);
    return EXIT_FAILURE;
  }

  if (!stbi_write_tga(pDst_filename, width, height, req_comps, pImage_data))
  {
    log_printf("failed writing image to file \"%s\"!\n", pDst_filename);
    free(pImage_data);
    return EXIT_FAILURE;
  }
  log_printf("wrote decompressed image to tga file \"%s\"\n", pDst_filename);
  
  log_printf("success.!!!\n");

  free(pImage_data);
  return EXIT_SUCCESS;
}

int main(int arg_c, char* ppArgs[])
{

  
  bool run_exhausive_test = false;
  bool test_memory_compression = false;
  bool optimize_huffman_tables = false;
  int subsampling = -1;
  char output_filename[256] = "";
  bool use_jpgd = true;
  bool test_jpgd_decompression = false;

  int arg_index = 1;
  while ((arg_index < arg_c) && (ppArgs[arg_index][0] == '-'))
  {
    switch (tolower(ppArgs[arg_index][1]))
    {
    case 'd':
      test_jpgd_decompression = true;
      break;
    case 'g':
      strcpy_s(s_log_filename, sizeof(s_log_filename), &ppArgs[arg_index][2]);
      break;
    case 'x':
      run_exhausive_test = true;
      break;
    case 'm':
      test_memory_compression = true;
      break;
    case 'o':
      optimize_huffman_tables = true;
      break;
    case 'l':
      if (strcasecmp(&ppArgs[arg_index][1], "luma") == 0)
        subsampling = jpge::Y_ONLY;
      else
      {
        log_printf("invalid option: %s\n", ppArgs[arg_index]);
        return EXIT_FAILURE;
      }
      break;
    case 'h':
      if (strcasecmp(&ppArgs[arg_index][1], "h1v1") == 0)
        subsampling = jpge::H1V1;
      else if (strcasecmp(&ppArgs[arg_index][1], "h2v1") == 0)
        subsampling = jpge::H2V1;
      else if (strcasecmp(&ppArgs[arg_index][1], "h2v2") == 0)
        subsampling = jpge::H2V2;
      else
      {
        log_printf("invalid subsampling: %s\n", ppArgs[arg_index]);
        return EXIT_FAILURE;
      }
      break;
    case 'w':
    {
      strcpy_s(output_filename, sizeof(output_filename), &ppArgs[arg_index][2]);
      break;
    }
    case 's':
    {
      use_jpgd = false;
      break;
    }
    default:
      log_printf("invalid option: %s\n", ppArgs[arg_index]);
      return EXIT_FAILURE;
    }
    arg_index++;
  }

  if (run_exhausive_test)
  {
    if ((arg_c - arg_index) < 1)
    {
      log_printf("not enough parameters (expected source file)\n");
      return print_usage();
    }

    const char* pSrc_filename = ppArgs[arg_index++];
    return exhausive_compression_test(pSrc_filename, use_jpgd);
  }
  else if (test_jpgd_decompression)
  {
    if ((arg_c - arg_index) < 2)
    {
      log_printf("not enough parameters (expected source and destination files)\n");
      return print_usage();
    }

    const char* pSrc_filename = ppArgs[arg_index++];
    const char* pDst_filename = ppArgs[arg_index++];
    return test_jpgd(pSrc_filename, pDst_filename);
  }


  if ((arg_c - arg_index) == 2)
  {
  }

  const char* pSrc_filename = ppArgs[arg_index++];
  const char* pDst_filename = ppArgs[arg_index++];

  int quality_factor = 75; //---------------->     compression level!!!!!!!   ;
  if ((quality_factor < 1) || (quality_factor > 100))
  {
    return EXIT_FAILURE;
  }

  const int req_comps = 3;
  int width = 0, height = 0, actual_comps = 0;
  uint8 *pImage_data = stbi_load(pSrc_filename, &width, &height, &actual_comps, req_comps);
  if (!pImage_data)
  {
    log_printf("failed loading file \"%s\"!\n", pSrc_filename);
    return EXIT_FAILURE;
  }


 
  jpge::params params;
  params.m_quality = quality_factor;
  params.m_subsampling = (subsampling < 0) ? ((actual_comps == 1) ? jpge::Y_ONLY : jpge::H2V2) : static_cast<jpge::subsampling_t>(subsampling);
  params.m_two_pass_flag = optimize_huffman_tables;

  log_printf("writing jpeg image to file: %s\n", pDst_filename);

  timer tm;
  
 
  if (test_memory_compression)
  {
    int buf_size = width * height * 3; 
    if (buf_size < 1024) buf_size = 1024;
    void *pBuf = malloc(buf_size);

    tm.start();
    if (!jpge::compress_image_to_jpeg_file_in_memory(pBuf, buf_size, width, height, req_comps, pImage_data, params))
    {
       log_printf("failed to create jpeg data!!!\n");
       return EXIT_FAILURE;
    }
    tm.stop();

    FILE *pFile = fopen(pDst_filename, "wb");
    if (!pFile)
    {
       log_printf("failed to create file \"%s\"!\n", pDst_filename);
       return EXIT_FAILURE;
    }

    if (fwrite(pBuf, buf_size, 1, pFile) != 1)
    {
       log_printf("failed writing to output file!\n");
       return EXIT_FAILURE;
    }

    if (fclose(pFile) == EOF)
    {
       log_printf("failed writing to output file!\n");
       return EXIT_FAILURE;
    }
  }
  else
  {
    tm.start();

    if (!jpge::compress_image_to_jpeg_file(pDst_filename, width, height, req_comps, pImage_data, params))
    {
       log_printf("failed writing to output file!\n");
       return EXIT_FAILURE;
    }
    tm.stop();
  }


 int uncomp_width = 0, uncomp_height = 0, uncomp_actual_comps = 0, uncomp_req_comps = 3;

  tm.start();
  uint8 *pUncomp_image_data;
  if (use_jpgd)
    pUncomp_image_data = jpgd::decompress_jpeg_image_from_file(pDst_filename, &uncomp_width, &uncomp_height, &uncomp_actual_comps, uncomp_req_comps);
  else
    pUncomp_image_data = stbi_load(pDst_filename, &uncomp_width, &uncomp_height, &uncomp_actual_comps, uncomp_req_comps);


  if (!pUncomp_image_data)
  {
    log_printf("failed to load compressed image file \"%s\"!\n", pDst_filename);
    return EXIT_FAILURE;
  }

  
 
  if (output_filename[0])
    stbi_write_tga(output_filename, uncomp_width, uncomp_height, uncomp_req_comps, pUncomp_image_data);

  if ((uncomp_width != width) || (uncomp_height != height))
  {
    log_printf("loaded jpeg file has different resolution than original!\n");
    return EXIT_FAILURE;
  }

  
  image_compare_results results;
  image_compare(results, width, height, pImage_data, req_comps, pUncomp_image_data, uncomp_req_comps, (params.m_subsampling == jpge::Y_ONLY) || (actual_comps == 1) || (uncomp_actual_comps == 1));

  log_printf("success.!!!\n");

  return EXIT_SUCCESS;
}
