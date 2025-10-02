#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <portaudio.h>
#include <SDL2/SDL.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

// Audio parameters
#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 256

// Visual parameters
#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 600
#define WAVE_SAMPLES 800
#define KNOB_RADIUS 30
#define KNOB_PANEL_HEIGHT 120

struct Knob {
    float x, y;
    float value;
    float minValue, maxValue;
    std::string label;
    bool isDragging;
    float dragStartY;
    float dragStartValue;
    
    Knob(float x, float y, float min, float max, float initial, const std::string& label) 
        : x(x), y(y), minValue(min), maxValue(max), value(initial), label(label),
          isDragging(false), dragStartY(0), dragStartValue(0) {}
    
    void update(int mouseX, int mouseY, bool mouseDown) {
        float dx = mouseX - x;
        float dy = mouseY - y;
        float distance = sqrt(dx*dx + dy*dy);
        
        if (mouseDown && distance <= KNOB_RADIUS && !isDragging) {
            isDragging = true;
            dragStartY = mouseY;
            dragStartValue = value;
        }
        
        if (isDragging) {
            if (mouseDown) {
                float deltaY = dragStartY - mouseY; // Inverted for intuitive control
                float sensitivity = (maxValue - minValue) / 100.0f; // Sensitivity factor
                value = dragStartValue + deltaY * sensitivity;
                value = std::max(minValue, std::min(maxValue, value));
            } else {
                isDragging = false;
            }
        }
    }
    
    void draw(SDL_Renderer* renderer) {
        // Draw knob base (dark circle)
        drawCircle(renderer, x, y, KNOB_RADIUS, 60, 60, 60);
        
        // Draw knob value indicator (bright circle)
        float angle = (value - minValue) / (maxValue - minValue) * 2 * M_PI * 0.8f - 0.8f * M_PI; // 288 degrees range
        int indicatorX = x + (KNOB_RADIUS - 8) * cos(angle);
        int indicatorY = y + (KNOB_RADIUS - 8) * sin(angle);
        drawCircle(renderer, indicatorX, indicatorY, 4, 255, 100, 100);
        
        // Draw border
        drawCircleOutline(renderer, x, y, KNOB_RADIUS, 200, 200, 200);
        
        // Draw label (simple text using lines)
        drawText(renderer, x - 25, y + KNOB_RADIUS + 10, label);
        
        // Draw value
        char valueStr[20];
        if (maxValue > 100) {
            snprintf(valueStr, sizeof(valueStr), "%.0f", value);
        } else {
            snprintf(valueStr, sizeof(valueStr), "%.2f", value);
        }
        drawText(renderer, x - 15, y + KNOB_RADIUS + 25, std::string(valueStr));
    }
    
private:
    void drawCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius, int r, int g, int b) {
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        for (int w = 0; w < radius * 2; w++) {
            for (int h = 0; h < radius * 2; h++) {
                int dx = radius - w;
                int dy = radius - h;
                if ((dx*dx + dy*dy) <= (radius * radius)) {
                    SDL_RenderDrawPoint(renderer, centerX + dx, centerY + dy);
                }
            }
        }
    }
    
    void drawCircleOutline(SDL_Renderer* renderer, int centerX, int centerY, int radius, int r, int g, int b) {
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        int x = radius - 1;
        int y = 0;
        int dx = 1;
        int dy = 1;
        int err = dx - (radius << 1);
        
        while (x >= y) {
            SDL_RenderDrawPoint(renderer, centerX + x, centerY + y);
            SDL_RenderDrawPoint(renderer, centerX + y, centerY + x);
            SDL_RenderDrawPoint(renderer, centerX - y, centerY + x);
            SDL_RenderDrawPoint(renderer, centerX - x, centerY + y);
            SDL_RenderDrawPoint(renderer, centerX - x, centerY - y);
            SDL_RenderDrawPoint(renderer, centerX - y, centerY - x);
            SDL_RenderDrawPoint(renderer, centerX + y, centerY - x);
            SDL_RenderDrawPoint(renderer, centerX + x, centerY - y);
            
            if (err <= 0) {
                y++;
                err += dy;
                dy += 2;
            }
            if (err > 0) {
                x--;
                dx += 2;
                err += dx - (radius << 1);
            }
        }
    }
    
    void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text) {
        // Simple bitmap-style text rendering
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        // This is a very basic implementation - you could use SDL_ttf for better text
        // For now, just draw a simple representation
        SDL_Rect rect = {x, y, (int)text.length() * 6, 10};
        SDL_RenderDrawRect(renderer, &rect);
    }
};

struct SawtoothData {
    float frequency;
    float phase;
    float phaseOffset;
    float amplitude;
    std::vector<float> waveBuffer;
    int bufferIndex;
    std::mutex bufferMutex;
    
    SawtoothData() : frequency(440.0f), phase(0.0f), phaseOffset(0.0f), amplitude(0.3f), 
                     waveBuffer(WAVE_SAMPLES, 0.0f), bufferIndex(0) {}
};

// Audio callback
static int sawtoothCallback(const void* inputBuffer, void* outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData) {
    
    SawtoothData* data = (SawtoothData*)userData;
    float* out = (float*)outputBuffer;
    
    std::lock_guard<std::mutex> lock(data->bufferMutex);
    
    for(unsigned long i = 0; i < framesPerBuffer; i++) {
        // Apply phase offset
        float adjustedPhase = fmod(data->phase + data->phaseOffset, 1.0f);
        if (adjustedPhase < 0) adjustedPhase += 1.0f;
        
        // Generate sawtooth wave
        float sample = (2.0f * adjustedPhase - 1.0f) * data->amplitude;
        
        if(i % 4 == 0) {
            data->waveBuffer[data->bufferIndex] = sample;
            data->bufferIndex = (data->bufferIndex + 1) % WAVE_SAMPLES;
        }
        
        *out++ = sample;
        *out++ = sample;
        
        // Update phase
        data->phase += data->frequency / SAMPLE_RATE;
        if(data->phase >= 1.0f) {
            data->phase -= 1.0f;
        }
    }
    
    return paContinue;
}

void drawWaveform(SDL_Renderer* renderer, SawtoothData& data) {
    std::lock_guard<std::mutex> lock(data.bufferMutex);
    
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // Red color
    
    int waveAreaHeight = WINDOW_HEIGHT - KNOB_PANEL_HEIGHT;
    int centerY = waveAreaHeight / 2;
    int scaleY = waveAreaHeight * 0.4f;
    
    for(int i = 0; i < WAVE_SAMPLES - 1; i++) {
        int sampleIndex1 = (data.bufferIndex + i) % WAVE_SAMPLES;
        int sampleIndex2 = (data.bufferIndex + i + 1) % WAVE_SAMPLES;
        
        float sample1 = data.waveBuffer[sampleIndex1];
        float sample2 = data.waveBuffer[sampleIndex2];
        
        int x1 = i * WINDOW_WIDTH / WAVE_SAMPLES;
        int y1 = centerY - (sample1 * scaleY);
        int x2 = (i + 1) * WINDOW_WIDTH / WAVE_SAMPLES;
        int y2 = centerY - (sample2 * scaleY);
        
        SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
    }
}

void drawGrid(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255); // Dark gray
    
    int waveAreaHeight = WINDOW_HEIGHT - KNOB_PANEL_HEIGHT;
    
    // Center line
    SDL_RenderDrawLine(renderer, 0, waveAreaHeight/2, WINDOW_WIDTH, waveAreaHeight/2);
    
    // Vertical lines
    for(int i = 0; i <= 10; i++) {
        int x = i * WINDOW_WIDTH / 10;
        SDL_RenderDrawLine(renderer, x, 0, x, waveAreaHeight);
    }
    
    // Horizontal lines
    for(int i = 0; i <= 8; i++) {
        int y = i * waveAreaHeight / 8;
        SDL_RenderDrawLine(renderer, 0, y, WINDOW_WIDTH, y);
    }
    
    // Separator line between waveform and controls
    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
    SDL_RenderDrawLine(renderer, 0, waveAreaHeight, WINDOW_WIDTH, waveAreaHeight);
}

void drawTitle(SDL_Renderer* renderer) {
    // Simple title - you could use SDL_ttf for better text
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect titleRect = {10, 10, 200, 20};
    SDL_RenderDrawRect(renderer, &titleRect);
    
    // Draw "Sawtooth Wave Generator" as simple text
    for(int i = 0; i < 20; i++) {
        SDL_RenderDrawPoint(renderer, 15 + i, 15);
        SDL_RenderDrawPoint(renderer, 15 + i, 25);
    }
}

std::atomic<int> handX(0), handY(0);
std::atomic<bool> handPinch(false);

void udpListener() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(5005);
    bind(sockfd, (sockaddr*)&addr, sizeof(addr));
    char buf[64];
    while (true) {
        int len = recv(sockfd, buf, sizeof(buf)-1, 0);
        if (len > 0) {
            buf[len] = 0;
            int x, y, pinch = 0;
            if (sscanf(buf, "%d,%d,%d", &x, &y, &pinch) >= 2) {
                handX = x;
                handY = y;
                handPinch = (pinch == 1);
            }
        }
    }
    close(sockfd);
}

int main() {
    // Initialize SDL
    if(SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return -1;
    }
    
    SDL_Window* window = SDL_CreateWindow("Sawtooth Wave Generator with Controls",
                                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                         WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    
    if(!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }
    
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    
    // Initialize audio
    PaStream* stream;
    PaError err;
    SawtoothData data;
    
    err = Pa_Initialize();
    if(err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    
    err = Pa_OpenDefaultStream(&stream, 0, 2, paFloat32, SAMPLE_RATE,
                              FRAMES_PER_BUFFER, sawtoothCallback, &data);
    
    if(err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    
    Pa_StartStream(stream);
    
    // Create knobs
    std::vector<Knob> knobs;
    int knobY = WINDOW_HEIGHT - KNOB_PANEL_HEIGHT/2;
    
    knobs.emplace_back(150, knobY, 50.0f, 2000.0f, 440.0f, "Frequency");
    knobs.emplace_back(350, knobY, 0.0f, 1.0f, 0.0f, "Phase");
    knobs.emplace_back(550, knobY, 0.0f, 1.0f, 0.3f, "Amplitude");
    
    std::cout << "Sawtooth wave generator with interactive knobs!" << std::endl;
    std::cout << "Click and drag knobs to adjust parameters:" << std::endl;
    std::cout << "- Frequency: 50-2000 Hz" << std::endl;
    std::cout << "- Phase: 0-1 (phase offset)" << std::endl;
    std::cout << "- Amplitude: 0-1 (volume)" << std::endl;
    std::cout << "Press ESC or close window to exit" << std::endl;
    
    // Start UDP listener thread
    std::thread listener(udpListener);
    listener.detach();
    
    // Main loop
    bool running = true;
    SDL_Event event;
    int mouseX = 0, mouseY = 0;
    bool mouseDown = false;
    
    while(running) {
        while(SDL_PollEvent(&event)) {
            if(event.type == SDL_QUIT) {
                running = false;
            }
            
            if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
            
            if(event.type == SDL_MOUSEBUTTONDOWN) {
                if(event.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = true;
                }
            }
            
            if(event.type == SDL_MOUSEBUTTONUP) {
                if(event.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = false;
                }
            }
            
            if(event.type == SDL_MOUSEMOTION) {
                mouseX = event.motion.x;
                mouseY = event.motion.y;
            }
        }
        
        // Update knobs and sync with audio data
        for(size_t i = 0; i < knobs.size(); i++) {
            knobs[i].update(handX, handY, handPinch); // Use handPinch instead of mouseDown
            
            // Update audio parameters based on knob values
            switch(i) {
                case 0: // Frequency
                    data.frequency = knobs[i].value;
                    break;
                case 1: // Phase
                    data.phaseOffset = knobs[i].value;
                    break;
                case 2: // Amplitude
                    data.amplitude = knobs[i].value;
                    break;
            }
        }
        
        // Clear screen (black background)
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        
        // Draw components
        drawTitle(renderer);
        drawGrid(renderer);
        drawWaveform(renderer, data);
        
        // Draw control panel background
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_Rect controlPanel = {0, WINDOW_HEIGHT - KNOB_PANEL_HEIGHT, WINDOW_WIDTH, KNOB_PANEL_HEIGHT};
        SDL_RenderFillRect(renderer, &controlPanel);
        
        // Draw knobs
        for(auto& knob : knobs) {
            knob.draw(renderer);
        }

        // Draw hand position indicator (semi-transparent circle)
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        if (handPinch) {
            SDL_SetRenderDrawColor(renderer, 255, 80, 180, 120); // Pink, alpha=120/255
        } else {
            SDL_SetRenderDrawColor(renderer, 0, 200, 255, 100); // Cyan, alpha=100/255
        }
        int radius = 25;
        for (int w = 0; w < radius * 2; w++) {
            for (int h = 0; h < radius * 2; h++) {
                int dx = radius - w;
                int dy = radius - h;
                if ((dx*dx + dy*dy) <= (radius * radius)) {
                    SDL_RenderDrawPoint(renderer, handX + dx, handY + dy);
                }
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        SDL_RenderPresent(renderer);
        
        SDL_Delay(16); // ~60 FPS
    }
    
    // Cleanup
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}

// Compilation (Linux):
// g++ -o sawtooth_knobs sawtooth_knobs.cpp -lportaudio -lSDL2 -lm
//
// Install: sudo apt-get install libsdl2-dev portaudio19-dev