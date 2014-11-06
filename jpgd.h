#ifndef JPEG_DECODER_H
#define JPEG_DECODER_H

#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#ifdef _MSC_VER
  #define JPGD_NORETURN __declspec(noreturn) 
#elif defined(__GNUC__)
  #define JPGD_NORETURN __attribute__ ((noreturn))
#else
  #define JPGD_NORETURN
#endif

namespace jpgd
{
  typedef unsigned char  uint8;
  typedef   signed short int16;
  typedef unsigned short uint16;
  typedef unsigned int   uint;
  typedef   signed int   int32;

  unsigned char *decompress_jpeg_image_from_memory(const unsigned char *pSrc_data, int src_data_size, int *width, int *height, int *actual_comps, int req_comps);
  unsigned char *decompress_jpeg_image_from_file(const char *pSrc_filename, int *width, int *height, int *actual_comps, int req_comps);

  
  enum jpgd_status
  {
    JPGD_SUCCESS = 0, JPGD_FAILED = -1, JPGD_DONE = 1,
    JPGD_BAD_DHT_COUNTS = -256, JPGD_BAD_DHT_INDEX, JPGD_BAD_DHT_MARKER, JPGD_BAD_DQT_MARKER, JPGD_BAD_DQT_TABLE, 
    JPGD_BAD_PRECISION, JPGD_BAD_HEIGHT, JPGD_BAD_WIDTH, JPGD_TOO_MANY_COMPONENTS, 
    JPGD_BAD_SOF_LENGTH, JPGD_BAD_VARIABLE_MARKER, JPGD_BAD_DRI_LENGTH, JPGD_BAD_SOS_LENGTH,
    JPGD_BAD_SOS_COMP_ID, JPGD_W_EXTRA_BYTES_BEFORE_MARKER, JPGD_NO_ARITHMITIC_SUPPORT, JPGD_UNEXPECTED_MARKER,
    JPGD_NOT_JPEG, JPGD_UNSUPPORTED_MARKER, JPGD_BAD_DQT_LENGTH, JPGD_TOO_MANY_BLOCKS,
    JPGD_UNDEFINED_QUANT_TABLE, JPGD_UNDEFINED_HUFF_TABLE, JPGD_NOT_SINGLE_SCAN, JPGD_UNSUPPORTED_COLORSPACE,
    JPGD_UNSUPPORTED_SAMP_FACTORS, JPGD_DECODE_ERROR, JPGD_BAD_RESTART_MARKER, JPGD_ASSERTION_ERROR,
    JPGD_BAD_SOS_SPECTRAL, JPGD_BAD_SOS_SUCCESSIVE, JPGD_STREAM_READ, JPGD_NOTENOUGHMEM
  };
    
  class jpeg_decoder_stream
  {
  public:
    jpeg_decoder_stream() { }
    virtual ~jpeg_decoder_stream() { }
virtual int read(uint8 *pBuf, int max_bytes_to_read, bool *pEOF_flag) = 0;
  };

  class jpeg_decoder_file_stream : public jpeg_decoder_stream
  {
    jpeg_decoder_file_stream(const jpeg_decoder_file_stream &);
    jpeg_decoder_file_stream &operator =(const jpeg_decoder_file_stream &);

    FILE *m_pFile;
    bool m_eof_flag, m_error_flag;

  public:
    jpeg_decoder_file_stream();
    virtual ~jpeg_decoder_file_stream();
    
    bool open(const char *Pfilename);
    void close();

    virtual int read(uint8 *pBuf, int max_bytes_to_read, bool *pEOF_flag);
  };

  
  class jpeg_decoder_mem_stream : public jpeg_decoder_stream
  {
    const uint8 *m_pSrc_data;
    uint m_ofs, m_size;

  public:
    jpeg_decoder_mem_stream() : m_pSrc_data(NULL), m_ofs(0), m_size(0) { }
    jpeg_decoder_mem_stream(const uint8 *pSrc_data, uint size) : m_pSrc_data(pSrc_data), m_ofs(0), m_size(size) { }

    virtual ~jpeg_decoder_mem_stream() { }

    bool open(const uint8 *pSrc_data, uint size);
    void close() { m_pSrc_data = NULL; m_ofs = 0; m_size = 0; }
    
    virtual int read(uint8 *pBuf, int max_bytes_to_read, bool *pEOF_flag);
  };

  
  unsigned char *decompress_jpeg_image_from_stream(jpeg_decoder_stream *pStream, int *width, int *height, int *actual_comps, int req_comps);

  enum 
  { 
    JPGD_IN_BUF_SIZE = 8192, JPGD_MAX_BLOCKS_PER_MCU = 10, JPGD_MAX_HUFF_TABLES = 8, JPGD_MAX_QUANT_TABLES = 4, 
    JPGD_MAX_COMPONENTS = 4, JPGD_MAX_COMPS_IN_SCAN = 4, JPGD_MAX_BLOCKS_PER_ROW = 8192, JPGD_MAX_HEIGHT = 16384, JPGD_MAX_WIDTH = 16384 
  };
          
  typedef int16 jpgd_quant_t;
  typedef int16 jpgd_block_t;

  class jpeg_decoder
  {
  public:
    
    jpeg_decoder(jpeg_decoder_stream *pStream);

    ~jpeg_decoder();

    int begin_decoding();

    int decode(const void** pScan_line, uint* pScan_line_len);
    
    inline jpgd_status get_error_code() const { return m_error_code; }

    inline int get_width() const { return m_image_x_size; }
    inline int get_height() const { return m_image_y_size; }

    inline int get_num_components() const { return m_comps_in_frame; }

    inline int get_bytes_per_pixel() const { return m_dest_bytes_per_pixel; }
    inline int get_bytes_per_scan_line() const { return m_image_x_size * get_bytes_per_pixel(); }

    
    inline int get_total_bytes_read() const { return m_total_bytes_read; }
    
  private:
    jpeg_decoder(const jpeg_decoder &);
    jpeg_decoder &operator =(const jpeg_decoder &);

    typedef void (*pDecode_block_func)(jpeg_decoder *, int, int, int);

    struct huff_tables
    {
      bool ac_table;
      uint  look_up[256];
      uint  look_up2[256];
      uint8 code_size[256];
      uint  tree[512];
    };

    struct coeff_buf
    {
      uint8 *pData;
      int block_num_x, block_num_y;
      int block_len_x, block_len_y;
      int block_size;
    };

    struct mem_block
    {
      mem_block *m_pNext;
      size_t m_used_count;
      size_t m_size;
      char m_data[1];
    };

    jmp_buf m_jmp_state;
    mem_block *m_pMem_blocks;
    int m_image_x_size;
    int m_image_y_size;
    jpeg_decoder_stream *m_pStream;
    int m_progressive_flag;
    uint8 m_huff_ac[JPGD_MAX_HUFF_TABLES];
    uint8* m_huff_num[JPGD_MAX_HUFF_TABLES];      
    uint8* m_huff_val[JPGD_MAX_HUFF_TABLES];      
    jpgd_quant_t* m_quant[JPGD_MAX_QUANT_TABLES]; 
    int m_scan_type;                              
    int m_comps_in_frame;                         
    int m_comp_h_samp[JPGD_MAX_COMPONENTS];       
    int m_comp_v_samp[JPGD_MAX_COMPONENTS];       
    int m_comp_quant[JPGD_MAX_COMPONENTS];        
    int m_comp_ident[JPGD_MAX_COMPONENTS];        
    int m_comp_h_blocks[JPGD_MAX_COMPONENTS];
    int m_comp_v_blocks[JPGD_MAX_COMPONENTS];
    int m_comps_in_scan;                          
    int m_comp_list[JPGD_MAX_COMPS_IN_SCAN];      
    int m_comp_dc_tab[JPGD_MAX_COMPONENTS];       
    int m_comp_ac_tab[JPGD_MAX_COMPONENTS];       
    int m_spectral_start;                         
    int m_spectral_end;                           
    int m_successive_low;                         
    int m_successive_high;                        
    int m_max_mcu_x_size;                         
    int m_max_mcu_y_size;                         
    int m_blocks_per_mcu;
    int m_max_blocks_per_row;
    int m_mcus_per_row, m_mcus_per_col;
    int m_mcu_org[JPGD_MAX_BLOCKS_PER_MCU];
    int m_total_lines_left;                       
    int m_mcu_lines_left;                         
    int m_real_dest_bytes_per_scan_line;
    int m_dest_bytes_per_scan_line;               
    int m_dest_bytes_per_pixel;                   
    huff_tables* m_pHuff_tabs[JPGD_MAX_HUFF_TABLES];
    coeff_buf* m_dc_coeffs[JPGD_MAX_COMPONENTS];
    coeff_buf* m_ac_coeffs[JPGD_MAX_COMPONENTS];
    int m_eob_run;
    int m_block_y_mcu[JPGD_MAX_COMPONENTS];
    uint8* m_pIn_buf_ofs;
    int m_in_buf_left;
    int m_tem_flag;
    bool m_eof_flag;
    uint8 m_in_buf_pad_start[128];
    uint8 m_in_buf[JPGD_IN_BUF_SIZE + 128];
    uint8 m_in_buf_pad_end[128];
    int m_bits_left;
    uint m_bit_buf;
    int m_restart_interval;
    int m_restarts_left;
    int m_next_restart_num;
    int m_max_mcus_per_row;
    int m_max_blocks_per_mcu;
    int m_expanded_blocks_per_mcu;
    int m_expanded_blocks_per_row;
    int m_expanded_blocks_per_component;
    bool  m_freq_domain_chroma_upsample;
    int m_max_mcus_per_col;
    uint m_last_dc_val[JPGD_MAX_COMPONENTS];
    jpgd_block_t* m_pMCU_coefficients;
    int m_mcu_block_max_zag[JPGD_MAX_BLOCKS_PER_MCU];
    uint8* m_pSample_buf;
    int m_crr[256];
    int m_cbb[256];
    int m_crg[256];
    int m_cbg[256];
    uint8* m_pScan_line_0;
    uint8* m_pScan_line_1;
    jpgd_status m_error_code;
    bool m_ready_flag;
    int m_total_bytes_read;

    void free_all_blocks();
    JPGD_NORETURN void stop_decoding(jpgd_status status);
    void *alloc(size_t n, bool zero = false);
    void word_clear(void *p, uint16 c, uint n);
    void prep_in_buffer();
    void read_dht_marker();
    void read_dqt_marker();
    void read_sof_marker();
    void skip_variable_marker();
    void read_dri_marker();
    void read_sos_marker();
    int next_marker();
    int process_markers();
    void locate_soi_marker();
    void locate_sof_marker();
    int locate_sos_marker();
    void init(jpeg_decoder_stream * pStream);
    void create_look_ups();
    void fix_in_buffer();
    void transform_mcu(int mcu_row);
    void transform_mcu_expand(int mcu_row);
    coeff_buf* coeff_buf_open(int block_num_x, int block_num_y, int block_len_x, int block_len_y);
    inline jpgd_block_t *coeff_buf_getp(coeff_buf *cb, int block_x, int block_y);
    void load_next_row();
    void decode_next_row();
    void make_huff_table(int index, huff_tables *pH);
    void check_quant_tables();
    void check_huff_tables();
    void calc_mcu_block_order();
    int init_scan();
    void init_frame();
    void process_restart();
    void decode_scan(pDecode_block_func decode_block_func);
    void init_progressive();
    void init_sequential();
    void decode_start();
    void decode_init(jpeg_decoder_stream * pStream);
    void H2V2Convert();
    void H2V1Convert();
    void H1V2Convert();
    void H1V1Convert();
    void gray_convert();
    void expanded_convert();
    void find_eoi();
    inline uint get_char();
    inline uint get_char(bool *pPadding_flag);
    inline void stuff_char(uint8 q);
    inline uint8 get_octet();
    inline uint get_bits(int num_bits);
    inline uint get_bits_no_markers(int numbits);
    inline int huff_decode(huff_tables *pH);
    inline int huff_decode(huff_tables *pH, int& extrabits);
    static inline uint8 clamp(int i);
    static void decode_block_dc_first(jpeg_decoder *pD, int component_id, int block_x, int block_y);
    static void decode_block_dc_refine(jpeg_decoder *pD, int component_id, int block_x, int block_y);
    static void decode_block_ac_first(jpeg_decoder *pD, int component_id, int block_x, int block_y);
    static void decode_block_ac_refine(jpeg_decoder *pD, int component_id, int block_x, int block_y);
  };
  
} 

#endif 
