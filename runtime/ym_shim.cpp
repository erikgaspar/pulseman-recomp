// Ponte em C para o YM2612 do ymfm, com os timers A/B contados por amostra.
#include <vector>
#include "ymfm_opn.h"

extern "C" {
#include "sound.h"
#include "savestate.h"
}

namespace {

class YmInterface : public ymfm::ymfm_interface {
public:
    // Tempo restante de cada timer, em ciclos de entrada do chip (-1 = parado)
    int64_t remaining[2] = {-1, -1};

    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override
    {
        remaining[tnum & 1] = duration_in_clocks;
    }

    // Avança os timers por 'clocks' ciclos e dispara os que vencerem
    void advance(int64_t clocks)
    {
        for (int t = 0; t < 2; t++) {
            if (remaining[t] < 0)
                continue;
            remaining[t] -= clocks;
            if (remaining[t] <= 0) {
                int64_t overshoot = -remaining[t];
                remaining[t] = -1;
                m_engine->engine_timer_expired(t);
                // o motor rearma o timer dentro do callback; desconta o atraso
                if (remaining[t] > 0)
                    remaining[t] -= overshoot;
            }
        }
    }

    void reset_timers() { remaining[0] = remaining[1] = -1; }
};

YmInterface intf;
ymfm::ym2612 chip(intf);

} // namespace

extern "C" void ym_reset(void)
{
    intf.reset_timers();
    chip.reset();
}

extern "C" void ym_write(int port, int data)
{
    chip.write(port & 3, (uint8_t)data);
}

extern "C" int ym_read_status(void)
{
    return chip.read_status();
}

extern "C" void ym_generate(int32_t out[2])
{
    ymfm::ym2612::output_data o;
    chip.generate(&o, 1);
    // cada amostra corresponde a 144 ciclos de entrada do chip
    intf.advance(144);
    out[0] = o.data[0];
    out[1] = o.data[1];
}

// Jogo salvo: estado do chip (formato do próprio ymfm) e os timers da ponte
extern "C" void ym_state(StateIO *s)
{
    std::vector<uint8_t> buf;
    uint32_t size = 0;
    if (!s->loading) {
        ymfm::ymfm_saved_state st(buf, true);
        chip.save_restore(st);
        size = (uint32_t)buf.size();
    }
    STATE_VAR(s, size);
    if (s->loading && (s->error || size > s->cap - s->pos))
        s->error = 1;
    if (s->loading && !s->error)
        buf.resize(size);
    if (size && !s->error)
        state_bytes(s, buf.data(), size);
    STATE_VAR(s, intf.remaining);
    if (s->loading && !s->error) {
        ymfm::ymfm_saved_state st(buf, false);
        chip.save_restore(st);
        chip.invalidate_caches();
    }
}
