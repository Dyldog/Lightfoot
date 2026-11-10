// tinyalsa's pcm_close() calls snd_utils_close_dev_node() even when plugins
// are compiled out (TINYALSA_USES_PLUGINS undefined). We never open plugin
// nodes, so the pointer is always null and a no-op is correct.
struct snd_node;

extern "C" void snd_utils_close_dev_node(struct snd_node*) {}
