#include <ox/ox.hpp>

namespace ox = ox;

struct Box : ox::Widget {
    bool toggled = false;
    bool selected = false;

    void paint_event(ox::Painter& p) override {
        auto bg = toggled ? ox::Color::Light_red : ox::Color::Black;
        if (selected)
            bg = ox::Color::Blue;

        auto b = ox::Brush{.background = bg};
        for (auto y : ox::range(this->height())) {
            for (auto x : ox::range(this->width())) {
                p.put(U' ', {x, y}, b);
            }
        }
    }
};

class Grid : public ox::layout::Grid<4, 8> {
   public:
    std::array<Box*, 32> boxes{};
    int r = 0, c = 0;

    Grid() {
        int idx = 0;
        for (auto& row : this->children()) {
            for (auto& cell : row->children()) {
                auto& b = cell->make_child<Box>();
                boxes[idx++] = &b;
            }
        }
        boxes[0]->selected = true;
    }

    bool key_press_event(ox::Key k) override {
        boxes[r * 8 + c]->selected = false;
        switch (k) {
            case ox::Key::Arrow_up:    r = (r - 1 + 4) % 4; break;
            case ox::Key::Arrow_down:  r = (r + 1) % 4; break;
            case ox::Key::Arrow_left:  c = (c - 1 + 8) % 8; break;
            case ox::Key::Arrow_right: c = (c + 1) % 8; break;
            case ox::Key::Space: boxes[r * 8 + c]->toggled = !boxes[r * 8 + c]->toggled; break;
            case ox::Key::q: ox::System::exit(); break;
            default: break;
        }
        boxes[r * 8 + c]->selected = true;
        this->update();
        return true;
    }
};

int main() {
    ox::System sys;
    sys.make_scene<Grid>();
    return sys.run();
}
