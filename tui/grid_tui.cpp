#include <ox/ox.hpp>
#include <cstdlib>

#define ROWS 4
#define COLS 16
using namespace ox;

class GridWidget : public Widget {
public:
    GridWidget() : Widget{FocusPolicy::Strong, SizePolicy{}} {
        // Initialize grid state (8x16)
        for (int i = 0; i < ROWS; ++i) {
            for (int j = 0; j < COLS; ++j) {
                grid_state_[i][j] = false;
            }
        }
        
        this->active = true;
    }

    void paint(Canvas canvas) override {
        // Clear canvas first
        for (int y = 0; y < canvas.size.height; ++y) {
            for (int x = 0; x < canvas.size.width; ++x) {
                canvas[{x, y}] = Glyph{U' '};
            }
        }
        
        // Draw the 8x16 grid
        for (int row = 0; row < ROWS; ++row) {
            for (int col = 0; col < COLS; ++col) {
                int x = col * 3;  // Each cell is 4 chars wide
                int y = row * 2;  // Each cell is 2 chars tall
                
                // Check bounds
                if (y + 1 >= canvas.size.height || x + 2 >= canvas.size.width) {
                    continue;
                }
                
                // Determine if this cell is highlighted
                bool is_highlighted = (row == current_row_ && col == current_col_);
                
                // Set colors
                auto cell_fg = is_highlighted ? XColor::Black : XColor::White;
                auto cell_bg = is_highlighted ? XColor::White : XColor::Black;
                
                // Draw top line of box
                canvas[{x, y}] = Glyph{U'┌'} | fg(cell_fg) | bg(cell_bg);
                canvas[{x + 1, y}] = Glyph{U'─'} | fg(cell_fg) | bg(cell_bg);
                canvas[{x + 2, y}] = Glyph{U'┐'} | fg(cell_fg) | bg(cell_bg);

                canvas[{x+0, y+1}] = Glyph{U' '} | fg(cell_fg) | bg(cell_bg);
                canvas[{x+2, y+1}] = Glyph{U' '} | fg(cell_fg) | bg(cell_bg);
                if (grid_state_[row][col]) {
                    canvas[{x+1, y+1}] = Glyph{U'█'} | fg(cell_fg) | bg(cell_bg);
                } else {
                    canvas[{x+1, y+1}] = Glyph{U' '} | fg(cell_fg) | bg(cell_bg);
                }
            }
        }

        
        for (int col = 0; col < COLS; ++col) {
            int x = col * 3;  // Each cell is 4 chars wide
            int y = (ROWS-1) * 2;  // Each cell is 2 chars tall
            canvas[{x, y + 2}] = Glyph{U'└'} | fg(XColor::White) | bg(XColor::Black);
            canvas[{x + 1, y + 2}] = Glyph{U'─'} | fg(XColor::White) | bg(XColor::Black);
            canvas[{x + 2, y + 2}] = Glyph{U'┘'} | fg(XColor::White) | bg(XColor::Black);
        }
    }

    void clearrow() {
        for (auto& cell : grid_state_[current_row_]) {
            cell = false;
        }
    }
    void togglecell() {
        grid_state_[current_row_][current_col_] = 
            !grid_state_[current_row_][current_col_];
    }
    
    void movecol(int n) {
        if (n > 0) {
            if (current_col_ < COLS-n) {
                current_col_+=n;
            } else {
                current_col_=COLS-1;
            }
        } else {
            if (current_col_ >= -n) {
                current_col_+=n;
            } else {
                current_col_=0;
            }
        }
    }

    void setnmovecol(int n) {
        if (!ctrl_held_) {
            togglecell();
        }
        movecol(n);
    }

    void key_press(Key k) override {        
        switch (k) {
            case Key::ArrowUp:
                if (current_row_ > 0) {
                    --current_row_;
                }
                break;
                
            case Key::ArrowDown:
                if (current_row_ < ROWS-1) {
                    ++current_row_;
                }
                break;
                
            case Key::ArrowLeft:
                if (!ctrl_held_)
                    movecol(-1);
                else
                    movecol(-4);
                break;
                
            case Key::ArrowRight:
                if (!ctrl_held_)
                    movecol(1);
                else
                    movecol(4);
                break;
                
            case Key::x:
                togglecell();
                break;

            case Key::C:
                clearrow();
                break;

            // set and move
            case Key::One:
                setnmovecol(1);
                break;
            
            case Key::Two:
                setnmovecol(2);
                break;
            
            case Key::Three:
                setnmovecol(3);
                break;
            
            case Key::Four:
                setnmovecol(4);
                break;
            
            case Key::Eight:
                setnmovecol(8);
                break;
            
            case Key::Home:
                current_col_=0;
                break;
            
            case Key::End:
                current_col_=COLS-1;
                break;

            
            case Key::LAlt:
                ctrl_held_ = true;
                break;
            
            case Key::q:
            case Key::Q:
                std::exit(0);
                break;
                
            default:
                Widget::key_press(k);
                break;
        }
    }


    void key_release(Key k) override {        
        switch (k) {
            case Key::LAlt:
                ctrl_held_ = false;
                break;
            
            default:
                Widget::key_press(k);
                break;
        }
    }                
    
    bool ctrl_held_ = false;
    int current_row_ = 0;
    int current_col_ = 0;
    bool grid_state_[8][16];
};

// Custom event handler that directly manages the widget
class GridApp {
public:
    GridApp() : widget_{}, timer_{std::chrono::milliseconds(50), true} {}
    
    auto handle_key_press(Key k) -> EventResponse {
        widget_.key_press(k);
        return std::nullopt;
    }

    auto handle_key_release(Key k) -> EventResponse {
        widget_.key_release(k);
        return std::nullopt;
    }

    auto handle_timer(int id) -> EventResponse {
        if (id == timer_.id()) {
            return std::nullopt; // Trigger repaint
        }
        return std::nullopt;
    }
    
    auto handle_resize(Area new_size) -> EventResponse {
        widget_.size = new_size;
        return std::nullopt;
    }
    
    auto handle_paint(Canvas canvas) -> Terminal::Cursor {
        widget_.paint(canvas);
        return std::nullopt;
    }

private:
    GridWidget widget_;
    Timer timer_;
};

int main() {
    Terminal term{MouseMode::Basic}; //, KeyMode::Raw};
    GridApp app{};
    return process_events(term, app);
}
