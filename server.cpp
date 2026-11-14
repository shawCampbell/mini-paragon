// Mini Paragon: an aircraft simulator and a tiny web server in one program.
//
// Build on Windows (MinGW):   g++ -std=c++17 server.cpp -o server.exe -pthread -lws2_32
// Build on Linux / macOS:     g++ -std=c++17 server.cpp -o server -pthread
// Run it from the mini-paragon folder, then open http://localhost:8080

#include "httplib.h"   // the web server library (one file, downloaded separately)
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Constants
const double PI = 3.14159265358979;
const double DEG_TO_RAD = PI / 180.0;
const double RAD_TO_DEG = 180.0 / PI;
const double METRES_PER_DEGREE = 111320.0;   // length of one degree of latitude
const int timeStepMillis = 100;

double turnInput = 0;    
double speedInput = 0;   
double altInput = 0;  

double speedInputMult = 1;
double altInputMult = 1;
double turnInputMult = 1;

// Data
struct Waypoint {
    double lat;
    double lon;
};

struct Aircraft {
    double lat = -34.15;        // degrees (negative = south)
    double lon = 18.85;         // degrees (positive = east)
    double alt = 100;           // metres
    double hdg = 0;             // heading in degrees, 0 = north, 90 = east
    double speed = 400;          // metres per second
    double maxTurnRate = 40;    // degrees per second
    double pitch = 0;
    double roll = 0;
    std::string autoPilot = "false";
};

// The whole simulation state lives in these global variables.
Aircraft ac;
std::vector<Waypoint> route = {
    {-34.15, 18.85},
    {-34.10, 18.95},
    {-34.18, 19.00},
    {-34.22, 18.88},
};
int nextWp = 1;                 // index of the waypoint we are flying towards

// The simulation runs in its own thread while the web server answers requests
// in others. This mutex makes sure only one of them touches `ac` at a time.
std::mutex simMutex;

// Navigation maths

// Compass direction (0 to 360) from the aircraft to a waypoint.
double bearingTo(Waypoint w) {
    double north = (w.lat - ac.lat) * METRES_PER_DEGREE;
    double east  = (w.lon - ac.lon) * METRES_PER_DEGREE * std::cos(ac.lat * DEG_TO_RAD);
    double bearing = std::atan2(east, north) * RAD_TO_DEG;
    if (bearing < 0) bearing += 360;
    return bearing;
}

// Distance in metres from the aircraft to a waypoint.
double distanceTo(Waypoint w) {
    double north = (w.lat - ac.lat) * METRES_PER_DEGREE;
    double east  = (w.lon - ac.lon) * METRES_PER_DEGREE * std::cos(ac.lat * DEG_TO_RAD);
    return std::sqrt(north * north + east * east);
}

// // Advance the simulation by `dt` seconds.
// void stepSim(double dt) {

//     if (ac.autoPilot)
//     {
//         // Close enough to the waypoint? Aim for the next one. The % wraps back to 0 at the end.
//         if (distanceTo(route[nextWp]) < 150) {
//             nextWp = (nextWp + 1) % route.size();
//         }

//         // How far must we turn? Between -180 and 180: negative = left, positive = right.
//         double error = bearingTo(route[nextWp]) - ac.hdg;
//         if (error > 180) error -= 360;
//         if (error < -180) error += 360;

//         // An aircraft can't turn instantly, so limit the turn for this time step.
//         double maxTurn = ac.maxTurnRate * dt;
//         if (error > maxTurn) error = maxTurn;
//         if (error < -maxTurn) error = -maxTurn;

//         // Turn, keeping the heading within 0 to 360.
//         ac.hdg += error;
//         if (ac.hdg >= 360) ac.hdg -= 360;
//         if (ac.hdg < 0) ac.hdg += 360;
//     }

//     // Move forward, then convert metres into degrees of latitude and longitude.
//     double distance = ac.speed * dt;
//     ac.lat += distance * std::cos(ac.hdg * DEG_TO_RAD) / METRES_PER_DEGREE;
//     ac.lon += distance * std::sin(ac.hdg * DEG_TO_RAD) / (METRES_PER_DEGREE * std::cos(ac.lat * DEG_TO_RAD));
// }

void flyStep(double dt) {
    if (ac.autoPilot != "true"){
        // What bank angle does this turn need? (a standard "coordinated turn" formula)
        double turnRate = turnInput * ac.maxTurnRate * DEG_TO_RAD;          // radians per second
        double targetRoll = std::atan(ac.speed * turnRate / 9.81) * RAD_TO_DEG;
        if (targetRoll > 30) targetRoll = 30;                                // cap the bank
        if (targetRoll < -30) targetRoll = -30;

        // What nose angle does this climb need?
        double climbRate = altInput * 20;                                    // metres per second (use your own number)
        double targetPitch = std::atan2(climbRate, ac.speed) * RAD_TO_DEG;

        // Ease towards the targets instead of snapping to them.
        ac.roll  += (targetRoll  - ac.roll)  * 3 * dt;
        ac.pitch += (targetPitch - ac.pitch) * 3 * dt;

        // if (_kbhit()){
        //     std::cout << "input buffer has something" << std::endl;
        //     int c = _getch();
        //     std::cout << c << std::endl;
        //     // if (c == 0 || c == 224){
        //     //     if (_getch() == 72){
        //     //         ac.speed += 1;
        //     //     }
        //     // } 
        //     if (c == 0 || c == 224){
        //         switch(_getch()){
        //             case 72: ac.speed += 100; break; // up
        //             case 80: ac.speed += -100; break; // down
        //             case 75: ac.hdg += -1; break; // left
        //             case 77: ac.hdg += 1; break; // right
        //         }
        //     }
        //     else if (c == ' '){
        //         ac.alt += 500;
        //     }
        //     else if (c == 27){
        //         ac.alt -= 500;
        //     }
        // }

        // Manual control
        ac.hdg += turnInput * turnInputMult * ac.maxTurnRate * dt;
        if (ac.hdg >= 360) ac.hdg -= 360;
        if (ac.hdg < 0) ac.hdg += 360;

        if (speedInput != 0){
            speedInputMult += 2;
        }
        else{
            speedInputMult = 1;
        }

        if (altInput != 0){
            altInputMult += 2;
        }
        else{
            altInputMult = 1;
        }

        if (turnInput != 0){
            turnInputMult += 2;
        }
        else{
            turnInputMult = 1;
        }

        ac.speed += speedInput * 10 * speedInputMult * dt;          // change speed by 10 m/s per second
        // if (ac.speed < 20) ac.speed = 20;          // keep it in a sensible range
        // if (ac.speed > 80) ac.speed = 80;

        ac.alt += altInput * 10 * altInputMult * dt;
    }
    else {
        // Close enough to the waypoint? Aim for the next one. The % wraps back to 0 at the end.
        if (distanceTo(route[nextWp]) < 150) {
            nextWp = (nextWp + 1) % route.size();
        }

        // How far must we turn? Between -180 and 180: negative = left, positive = right.
        double error = bearingTo(route[nextWp]) - ac.hdg;
        if (error > 180) error -= 360;
        if (error < -180) error += 360;

        // An aircraft can't turn instantly, so limit the turn for this time step.
        double maxTurn = ac.maxTurnRate * dt;
        if (error > maxTurn) error = maxTurn;
        if (error < -maxTurn) error = -maxTurn;

        // Turn, keeping the heading within 0 to 360.
        ac.hdg += error;
        if (ac.hdg >= 360) ac.hdg -= 360;
        if (ac.hdg < 0) ac.hdg += 360;
    }

    double distance = ac.speed * dt;
    ac.lat += distance * std::cos(ac.hdg * DEG_TO_RAD) / METRES_PER_DEGREE;
    ac.lon += distance * std::sin(ac.hdg * DEG_TO_RAD) / (METRES_PER_DEGREE * std::cos(ac.lat * DEG_TO_RAD));
}

// Runs forever in its own thread: 20 simulation steps per second.
void simLoop() {
    const double dt = 0.001;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(simMutex);   // locked until the closing brace
            flyStep(dt);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    
}


// JSON helper: field("lat", 5.5) gives  "lat":5.500000
std::string field(std::string name, double value) {
    return "\"" + name + "\":" + std::to_string(value);
}

std::string field(std::string name, std::string value) {
    return "\"" + name + "\":" + value;
}


// main: start the simulation, then run the web server
int main() {
    // std::thread simThread(simLoop);
    std::thread simThreadFly(simLoop);
    // simThread.detach();                 // let it run in the background
    simThreadFly.detach();
    httplib::Server server;

    // The browser asks for this ten times a second.
    server.Get("/api/telemetry", [](const httplib::Request&, httplib::Response& res) {
        std::string json;
        {
            std::lock_guard<std::mutex> lock(simMutex);
            json = "{" + field("lat", ac.lat) + "," +
                         field("lon", ac.lon) + "," +
                         field("alt", ac.alt) + "," +
                         field("hdg", ac.hdg) + "," +
                         field("pitch", ac.pitch) + "," +
                         field("roll", ac.roll) + "," +
                         field("speed", ac.speed) + "," +
                         field("autoPilot", ac.autoPilot) + "," +
                         field("wp", nextWp) + "}";
        }
        res.set_content(json, "application/json");
    });

    // The browser asks for this once, to draw the planned route.
    server.Get("/api/route", [](const httplib::Request&, httplib::Response& res) {
        std::string json = "[";
        for (Waypoint w : route) {
            if (json != "[") json += ",";
            json += "{" + field("lat", w.lat) + "," + field("lon", w.lon) + "}";
        }
        json += "]";
        res.set_content(json, "application/json");
    });

    server.Get("/api/settings", [](const httplib::Request&, httplib::Response& res) {
            std::string json;
            {
                std::lock_guard<std::mutex> lock(simMutex);
                json = "{" + field("autoPilot", ac.autoPilot) + "}";
            }
            res.set_content(json, "application/json");
        });

    server.Post("/api/control", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(simMutex);
        if (req.has_param("aut") && req.get_param_value("aut") == "true"){
            ac.autoPilot = "true";
        }
        else{
            ac.autoPilot = "false";
            if (req.has_param("turn"))  turnInput  = std::stod(req.get_param_value("turn"));
            if (req.has_param("speed")) speedInput = std::stod(req.get_param_value("speed"));
            if (req.has_param("alt")) altInput = std::stod(req.get_param_value("alt"));
        }
        res.set_content("ok", "text/plain");
    });

    // Serve the web page: everything in the "web" folder.
    if (!server.set_mount_point("/", "./web")) {
        std::cout << "ERROR: can't find the 'web' folder. Run this from the mini-paragon folder." << std::endl;
        return 1;
    }

    std::cout << "Running at http://localhost:8080  (Ctrl+C to stop)" << std::endl;
    if (!server.listen("127.0.0.1", 8080)) {
        std::cout << "ERROR: couldn't start on port 8080. Is something else using it?" << std::endl;
        return 1;
    }
}