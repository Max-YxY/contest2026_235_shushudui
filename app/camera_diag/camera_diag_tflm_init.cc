/****************************************************************************
 * apps/examples/camera_diag/camera_diag_tflm_init.cc
 *
 * M11: validate that TFLite Micro can allocate tensors using the model stored
 * outside the boot image.  This diagnostic deliberately performs no invoke,
 * camera access, display updates, or touch handling.
 ****************************************************************************/

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <nuttx/clock.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "camera_diag_tflm_preprocess.h"

extern "C"
{
#include "camera_diag_model_storage.h"
}

/* The NuttX libc++ configuration used by this board has no global allocation
 * operators. TFLite Micro emits sized delete calls, so provide the same thin
 * libc bridge used by the existing C++ diagnostic application. */
void *operator new(size_t size) { return malloc(size); }
void *operator new[](size_t size) { return malloc(size); }
void operator delete(void *ptr) noexcept { free(ptr); }
void operator delete[](void *ptr) noexcept { free(ptr); }
void operator delete(void *ptr, size_t) noexcept { free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { free(ptr); }

namespace
{

constexpr size_t kTensorArenaBytes = 8U * 1024U * 1024U;
constexpr uint32_t kMallocCap8Bit = 1U << 2;
constexpr uint32_t kMallocCapSpiram = 1U << 10;
static int g_last_nms_count = -1;
static int g_last_defect_nms_count = -1;
/* Energy-level classes 0..4 share the same decoded candidates as defects. */
static int g_last_energy_level = -1;
static bool g_last_label_detected = false;

extern "C" void *heap_caps_malloc(size_t size, uint32_t caps);
extern "C" void heap_caps_free(void *ptr);

static void print_tensor(const char *name, const TfLiteTensor *tensor)
{
  int i;

  if (tensor == nullptr || tensor->dims == nullptr)
    {
      printf("tflm_init: %s unavailable\n", name);
      return;
    }

  printf("tflm_init: %s type=%d bytes=%lu dims=", name, tensor->type,
         static_cast<unsigned long>(tensor->bytes));
  for (i = 0; i < tensor->dims->size; i++)
    {
      printf("%s%d", i == 0 ? "" : "x", tensor->dims->data[i]);
    }

  printf(" scale=%g zero_point=%ld\n", static_cast<double>(tensor->params.scale),
         static_cast<long>(tensor->params.zero_point));
}

struct yolo_candidate_s
{
  int score;
  int class_id;
  int x;
  int y;
  int w;
  int h;
};

static const char *yolo_class_name(int class_id)
{
  static const char *const names[] =
  {
    "level_1", "level_2", "level_3", "level_4", "level_5",
    "stain", "damage", "wrinkle", "label", "box"
  };

  return class_id >= 0 && class_id < 10 ? names[class_id] : "unknown";
}

static bool yolo_is_defect(int class_id)
{
  return class_id == 5 || class_id == 6 || class_id == 7;
}

static int yolo_energy_level(const yolo_candidate_s *kept, int kept_count)
{
  int best_score = -129;
  int level = -1;

  for (int i = 0; i < kept_count; i++)
    {
      if (kept[i].class_id >= 0 && kept[i].class_id <= 4 &&
          kept[i].score > best_score)
        {
          best_score = kept[i].score;
          level = kept[i].class_id + 1;
        }
    }

  return level;
}

static bool yolo_has_class(const yolo_candidate_s *kept, int kept_count,
                           int class_id)
{
  for (int i = 0; i < kept_count; i++)
    if (kept[i].class_id == class_id)
      return true;

  return false;
}

static int yolo_defect_count(const yolo_candidate_s *kept, int kept_count)
{
  int defect_count = 0;

  for (int i = 0; i < kept_count; i++)
    {
      if (yolo_is_defect(kept[i].class_id))
        {
          defect_count++;
        }
    }

  return defect_count;
}

constexpr int kYoloRawScoreThreshold = -10;

static int yolo_nms(const int8_t *data, yolo_candidate_s *kept, int kept_cap)
{
  yolo_candidate_s candidates[16];
  int count = 0;
  int kept_count = 0;
  for (int anchor = 0; anchor < 1344; anchor++)
    {
      int best = -128;
      int class_id = 0;
      for (int cls = 0; cls < 10; cls++)
        {
          int score = data[(4 + cls) * 1344 + anchor];
          if (score > best) { best = score; class_id = cls; }
        }
      if (best >= kYoloRawScoreThreshold && count < 16)
        {
          candidates[count++] = { best, class_id, data[anchor] + 128,
            data[1344 + anchor] + 128, data[2 * 1344 + anchor] + 128,
            data[3 * 1344 + anchor] + 128 };
        }
    }
  for (int i = 0; i < count; i++)
    for (int j = i + 1; j < count; j++)
      if (candidates[j].score > candidates[i].score)
        { yolo_candidate_s t = candidates[i]; candidates[i] = candidates[j]; candidates[j] = t; }
  for (int i = 0; i < count && kept_count < kept_cap; i++)
    {
      bool suppress = false;
      for (int j = 0; j < kept_count; j++)
        if (candidates[i].class_id == kept[j].class_id)
          {
            int ax0 = candidates[i].x - candidates[i].w / 2, ax1 = candidates[i].x + candidates[i].w / 2;
            int ay0 = candidates[i].y - candidates[i].h / 2, ay1 = candidates[i].y + candidates[i].h / 2;
            int bx0 = kept[j].x - kept[j].w / 2, bx1 = kept[j].x + kept[j].w / 2;
            int by0 = kept[j].y - kept[j].h / 2, by1 = kept[j].y + kept[j].h / 2;
            int iw = (ax1 < bx1 ? ax1 : bx1) - (ax0 > bx0 ? ax0 : bx0);
            int ih = (ay1 < by1 ? ay1 : by1) - (ay0 > by0 ? ay0 : by0);
            int inter = iw > 0 && ih > 0 ? iw * ih : 0;
            int uni = candidates[i].w * candidates[i].h + kept[j].w * kept[j].h - inter;
            if (uni > 0 && inter * 100 >= uni * 45) suppress = true;
          }
      if (!suppress) kept[kept_count++] = candidates[i];
    }
  return kept_count;
}

static void yolo_set_synthetic_candidate(int8_t *data, int anchor,
                                         int class_id, int score,
                                         int x, int y, int w, int h)
{
  data[anchor] = static_cast<int8_t>(x - 128);
  data[1344 + anchor] = static_cast<int8_t>(y - 128);
  data[2 * 1344 + anchor] = static_cast<int8_t>(w - 128);
  data[3 * 1344 + anchor] = static_cast<int8_t>(h - 128);
  data[(4 + class_id) * 1344 + anchor] = static_cast<int8_t>(score);
}

}  // namespace

extern "C" int camera_diag_tflm_nms_selftest(void)
{
  /* This test exercises the deployed 1x14x1344 int8 layout without camera,
   * model, display, or Flash access.  The same-class overlap must suppress,
   * a different-class overlap must remain, and score -6 must be rejected. */
  static int8_t synthetic[14 * 1344];
  yolo_candidate_s kept[5];
  int kept_count;

  memset(synthetic, -128, sizeof(synthetic));
  yolo_set_synthetic_candidate(synthetic, 0, 2, 100, 100, 100, 80, 80);
  yolo_set_synthetic_candidate(synthetic, 1, 2, 90, 104, 104, 80, 80);
  yolo_set_synthetic_candidate(synthetic, 2, 7, 80, 100, 100, 80, 80);
  yolo_set_synthetic_candidate(synthetic, 3, 2, -11, 180, 180, 40, 40);
  yolo_set_synthetic_candidate(synthetic, 4, 0, 70, 200, 200, 40, 40);
  yolo_set_synthetic_candidate(synthetic, 5, 9, 60, 240, 240, 40, 40);

  kept_count = yolo_nms(synthetic, kept, 5);
  const int defect_count = yolo_defect_count(kept, kept_count);
  const int energy_level = yolo_energy_level(kept, kept_count);
  const bool pass = kept_count == 4 && kept[0].class_id == 2 &&
                    kept[0].score == 100 && kept[1].class_id == 7 &&
                    kept[1].score == 80 && defect_count == 1 &&
                    energy_level == 3;
  printf("tflm_nms_selftest: same_class_overlap=suppressed "
         "cross_class_overlap=kept low_score_rejected=1 "
         "structural_ignored=1 energy_level=%d defects=%d kept=%d result=%s\n",
         energy_level, defect_count, kept_count, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

extern "C" int camera_diag_tflm_last_nms_count(void)
{
  return g_last_nms_count;
}

extern "C" int camera_diag_tflm_last_defect_nms_count(void)
{
  return g_last_defect_nms_count;
}

extern "C" int camera_diag_tflm_last_energy_level(void)
{
  return g_last_energy_level;
}

extern "C" bool camera_diag_tflm_last_label_detected(void)
{
  return g_last_label_detected;
}

extern "C" int camera_diag_tflm_init_diag(void)
{
  const uint8_t *model_data = nullptr;
  size_t model_len = 0;
  uint8_t *tensor_arena = nullptr;
  const tflite::Model *model;
  tflite::MicroMutableOpResolver<20> resolver;
  int status = 1;
  clock_t start_ticks;
  clock_t elapsed_ticks;

  if (camera_diag_model_storage_load_psram(&model_data, &model_len) != 0)
    {
      printf("tflm_init: external model acquire failed\n");
      return 1;
    }

  model = tflite::GetModel(model_data);
  if (model == nullptr)
    {
      printf("tflm_init: GetModel failed\n");
      goto out;
    }

  if (model->version() != TFLITE_SCHEMA_VERSION)
    {
      printf("tflm_init: schema mismatch\n");
      goto out;
    }

  if (resolver.AddConv2D(tflite::Register_CONV_2D_INT8()) != kTfLiteOk ||
      resolver.AddMaxPool2D(tflite::Register_MAX_POOL_2D_INT8()) != kTfLiteOk ||
      resolver.AddPad() != kTfLiteOk || resolver.AddLogistic() != kTfLiteOk ||
      resolver.AddMul() != kTfLiteOk || resolver.AddAdd() != kTfLiteOk ||
      resolver.AddStridedSlice() != kTfLiteOk ||
      resolver.AddResizeNearestNeighbor() != kTfLiteOk ||
      resolver.AddConcatenation() != kTfLiteOk || resolver.AddSub() != kTfLiteOk ||
      resolver.AddTranspose() != kTfLiteOk || resolver.AddQuantize() != kTfLiteOk ||
      resolver.AddReshape() != kTfLiteOk || resolver.AddSoftmax() != kTfLiteOk)
    {
      printf("tflm_init: operator resolver setup failed\n");
      goto out;
    }

  tensor_arena = static_cast<uint8_t *>(heap_caps_malloc(
      kTensorArenaBytes, kMallocCapSpiram | kMallocCap8Bit));
  if (tensor_arena == nullptr)
    {
      printf("tflm_init: PSRAM arena allocation failed bytes=%lu\n",
             static_cast<unsigned long>(kTensorArenaBytes));
      goto out;
    }

  {
    tflite::MicroInterpreter interpreter(model, resolver, tensor_arena,
                                          kTensorArenaBytes);
    start_ticks = clock_systime_ticks();
    TfLiteStatus allocate_status = interpreter.AllocateTensors();
    elapsed_ticks = clock_systime_ticks() - start_ticks;
    printf("tflm_init: AllocateTensors status=%d elapsed_ms=%lu arena_used=%lu\n",
           allocate_status,
           static_cast<unsigned long>((elapsed_ticks * 1000UL) / TICK_PER_SEC),
           static_cast<unsigned long>(interpreter.arena_used_bytes()));
    if (allocate_status != kTfLiteOk)
      {
        goto out;
      }

    print_tensor("input", interpreter.input(0));
    print_tensor("output", interpreter.output(0));
    status = 0;
  }

out:
  if (tensor_arena != nullptr)
    {
      heap_caps_free(tensor_arena);
    }

  camera_diag_model_storage_free_psram(model_data);

  return status;
}

extern "C" int camera_diag_tflm_preprocess_diag(const uint8_t *rgb565,
                                                 size_t frame_len,
                                                 int frame_width,
                                                 int frame_height)
{
  const uint8_t *model_data = nullptr;
  size_t model_len = 0;
  uint8_t *tensor_arena = nullptr;
  const tflite::Model *model;
  tflite::MicroMutableOpResolver<20> resolver;
  int status = 1;

  g_last_nms_count = -1;
  g_last_defect_nms_count = -1;
  g_last_energy_level = -1;
  g_last_label_detected = false;

  if (rgb565 == nullptr || frame_width <= 0 || frame_height <= 0 ||
      camera_diag_model_storage_load_psram(&model_data, &model_len) != 0)
    {
      return 1;
    }

  model = tflite::GetModel(model_data);
  if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION ||
      resolver.AddConv2D(tflite::Register_CONV_2D_INT8()) != kTfLiteOk ||
      resolver.AddMaxPool2D(tflite::Register_MAX_POOL_2D_INT8()) != kTfLiteOk ||
      resolver.AddPad() != kTfLiteOk || resolver.AddLogistic() != kTfLiteOk ||
      resolver.AddMul() != kTfLiteOk || resolver.AddAdd() != kTfLiteOk ||
      resolver.AddStridedSlice() != kTfLiteOk ||
      resolver.AddResizeNearestNeighbor() != kTfLiteOk ||
      resolver.AddConcatenation() != kTfLiteOk || resolver.AddSub() != kTfLiteOk ||
      resolver.AddTranspose() != kTfLiteOk || resolver.AddQuantize() != kTfLiteOk ||
      resolver.AddReshape() != kTfLiteOk || resolver.AddSoftmax() != kTfLiteOk)
    {
      goto out;
    }

  tensor_arena = static_cast<uint8_t *>(heap_caps_malloc(
      kTensorArenaBytes, kMallocCapSpiram | kMallocCap8Bit));
  if (tensor_arena == nullptr)
    {
      goto out;
    }

  {
    tflite::MicroInterpreter interpreter(model, resolver, tensor_arena,
                                          kTensorArenaBytes);
    if (interpreter.AllocateTensors() != kTfLiteOk)
      {
        goto out;
      }

    TfLiteTensor *input = interpreter.input(0);
    uint32_t checksum = 0;
    int8_t minimum = 0;
    int8_t maximum = 0;
    int rc;
    if (input == nullptr || input->type != kTfLiteInt8 ||
        input->data.int8 == nullptr || input->dims == nullptr ||
        input->bytes != 256U * 256U * 3U)
      {
        goto out;
      }

    rc = camera_diag_tflm_preprocess_rgb565(
        rgb565, frame_len, frame_width, frame_height, input->data.int8,
        input->bytes, input->dims->data[2], input->dims->data[1],
        input->params.scale, input->params.zero_point, &checksum, &minimum,
        &maximum);
    printf("tflm_preprocess: rc=%d checksum=0x%08lx min=%d max=%d bytes=%lu\n",
           rc, static_cast<unsigned long>(checksum), static_cast<int>(minimum),
           static_cast<int>(maximum), static_cast<unsigned long>(input->bytes));
    status = rc == 0 ? 0 : 1;

    /* M13 probe: invoke the same interpreter after the real-frame input has
     * been prepared. Keep this diagnostic bounded to tensor metadata and
     * integer statistics until the output layout is confirmed on-device. */
    if (status == 0)
      {
        const clock_t invoke_start = clock_systime_ticks();
        const TfLiteStatus invoke_status = interpreter.Invoke();
        const clock_t invoke_elapsed = clock_systime_ticks() - invoke_start;
        const TfLiteTensor *output = interpreter.output(0);
        int raw_min = 127;
        int raw_max = -128;
        uint32_t output_checksum = 0;
        size_t index;

        if (output != nullptr && output->type == kTfLiteInt8 &&
            output->data.int8 != nullptr)
          {
            for (index = 0; index < output->bytes; index++)
              {
                const int value = output->data.int8[index];
                raw_min = value < raw_min ? value : raw_min;
                raw_max = value > raw_max ? value : raw_max;
                output_checksum = (output_checksum * 33U) ^
                                  static_cast<uint8_t>(value);
              }

            /* The frozen external model is 1x14x1344: channels 0..3 are
             * normalized box values and channels 4..13 map to the ten
             * documented label classes. Its verified output quantization is
             * scale 0.005286871 and zero-point -128. The M16 recall
             * candidate uses raw -10, selected by an ESP-equivalent sweep.
             * Then run bounded per-class NMS over the same verified layout. */
            int best_raw = -128;
            size_t best_anchor = 0;
            size_t best_class = 0;
            if (output->dims != nullptr && output->dims->size == 3 &&
                output->dims->data[0] == 1 && output->dims->data[1] == 14 &&
                output->dims->data[2] == 1344)
              {
                for (size_t anchor = 0; anchor < 1344; anchor++)
                  {
                    for (size_t class_id = 0; class_id < 10; class_id++)
                      {
                        const int raw_score = output->data.int8[
                          (4 + class_id) * 1344 + anchor];
                        if (raw_score > best_raw)
                          {
                            best_raw = raw_score;
                            best_anchor = anchor;
                            best_class = class_id;
                          }
                      }
                  }
                yolo_candidate_s kept[5];
                const int kept_count = yolo_nms(output->data.int8, kept, 5);
                g_last_nms_count = kept_count;
                g_last_defect_nms_count = yolo_defect_count(kept, kept_count);
                g_last_energy_level = yolo_energy_level(kept, kept_count);
                g_last_label_detected = yolo_has_class(kept, kept_count, 8);
                printf("tflm_decode: top_anchor=%lu class=%lu name=%s "
                       "defect=%d score_raw=%d threshold_raw=%d accepted=%d "
                       "nms_kept=%d defects=%d\n",
                       static_cast<unsigned long>(best_anchor),
                       static_cast<unsigned long>(best_class),
                       yolo_class_name(best_class), yolo_is_defect(best_class),
                       best_raw, kYoloRawScoreThreshold,
                       best_raw >= kYoloRawScoreThreshold, kept_count,
                       g_last_defect_nms_count);
                printf("tflm_decode: energy_level=%d\n", g_last_energy_level);
                printf("tflm_decode: label_detected=%d\n", g_last_label_detected);
                for (int result = 0; result < kept_count; result++)
                  printf("tflm_nms: class=%d name=%s defect=%d score_raw=%d "
                         "box_q=%d,%d,%d,%d\n", kept[result].class_id,
                         yolo_class_name(kept[result].class_id),
                         yolo_is_defect(kept[result].class_id), kept[result].score,
                         kept[result].x, kept[result].y,
                         kept[result].w, kept[result].h);
              }
          }

        printf("tflm_invoke: status=%d elapsed_ms=%lu output_type=%d bytes=%lu "
               "scale=%g zero_point=%ld raw_min=%d raw_max=%d checksum=0x%08lx dims=",
               invoke_status,
               static_cast<unsigned long>((invoke_elapsed * 1000UL) /
                                           TICK_PER_SEC),
               output == nullptr ? -1 : output->type,
               output == nullptr ? 0UL : static_cast<unsigned long>(output->bytes),
               output == nullptr ? 0.0 : static_cast<double>(output->params.scale),
               output == nullptr ? 0L : static_cast<long>(output->params.zero_point),
               raw_min, raw_max, static_cast<unsigned long>(output_checksum));
        if (output != nullptr && output->dims != nullptr)
          {
            for (index = 0; index < static_cast<size_t>(output->dims->size);
                 index++)
              {
                printf("%s%d", index == 0 ? "" : ",",
                       output->dims->data[index]);
              }
          }
        printf("\n");
        if (invoke_status != kTfLiteOk)
          {
            status = 1;
          }
      }
  }

out:
  if (tensor_arena != nullptr)
    {
      heap_caps_free(tensor_arena);
    }
  camera_diag_model_storage_free_psram(model_data);
  return status;
}
