#include <GL/freeglut.h>
#include <tiny_obj_loader.h>
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <cmath>
#include <cfloat>

using json = nlohmann::json;
int lastMouseX = 0, lastMouseY = 0;
bool leftButtonDown = false;
bool rightButtonDown = false;


struct Vec3 {
    float x, y, z;
};

std::vector<float> brainVertices;
std::vector<unsigned int> brainIndices;
std::map<std::string, Vec3> electrodePositions;
std::map<std::string, float> activityValues;

Vec3 brainMin = { FLT_MAX, FLT_MAX, FLT_MAX };
Vec3 brainMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
Vec3 electrodesMin = { FLT_MAX, FLT_MAX, FLT_MAX };
Vec3 electrodesMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
Vec3 brainCentre = { 0, 0, 0 };

float rotationY = 0.0f;
float zoom = -2.5f;
float rotateX = 0.0f;
float rotateY = 0.0f;

void loadBrainModel(const std::string& filename) {
    std::cout << "Loading brain model from " << filename << std::endl;
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str());
    if (!success) {
        std::cerr << "Failed to load OBJ: " << err << "\n";
        exit(1);
    }

    for (const auto& shape : shapes) {
        for (auto idx : shape.mesh.indices) {
            float x = attrib.vertices[3 * idx.vertex_index + 0];
            float y = attrib.vertices[3 * idx.vertex_index + 1];
            float z = attrib.vertices[3 * idx.vertex_index + 2];

            brainVertices.push_back(x);
            brainVertices.push_back(y);
            brainVertices.push_back(z);
            brainIndices.push_back(brainIndices.size());

            brainMin.x = std::min(brainMin.x, x);
            brainMin.y = std::min(brainMin.y, y);
            brainMin.z = std::min(brainMin.z, z);
            brainMax.x = std::max(brainMax.x, x);
            brainMax.y = std::max(brainMax.y, y);
            brainMax.z = std::max(brainMax.z, z);
        }
    }

    brainCentre.x = (brainMin.x + brainMax.x) / 2.0f;
    brainCentre.y = (brainMin.y + brainMax.y) / 2.0f;
    brainCentre.z = (brainMin.z + brainMax.z) / 2.0f;

    std::cout << "Loaded " << brainVertices.size() / 3 << " vertices.\n";
}

void loadElectrodePositions(const std::string& filename) {
    std::cout << "Loading electrode positions from " << filename << std::endl;
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Failed to open electrode positions file.\n";
        exit(1);
    }

    json data;
    file >> data;

    for (auto& [label, coords] : data.items()) {
        float x = coords[0], y = coords[1], z = coords[2];
        electrodePositions[label] = { x, y, z };

        electrodesMin.x = std::min(electrodesMin.x, x);
        electrodesMin.y = std::min(electrodesMin.y, y);
        electrodesMin.z = std::min(electrodesMin.z, z);
        electrodesMax.x = std::max(electrodesMax.x, x);
        electrodesMax.y = std::max(electrodesMax.y, y);
        electrodesMax.z = std::max(electrodesMax.z, z);
    }

    std::cout << "Loaded " << electrodePositions.size() << " electrodes.\n";
}

void normalizeElectrodePositions() {
    Vec3 brainSize = {
        brainMax.x - brainMin.x,
        brainMax.y - brainMin.y,
        brainMax.z - brainMin.z
    };

    Vec3 electrodesSize = {
        electrodesMax.x - electrodesMin.x,
        electrodesMax.y - electrodesMin.y,
        electrodesMax.z - electrodesMin.z
    };

    for (auto& [label, pos] : electrodePositions) {
        pos.x = brainMin.x + ((pos.x - electrodesMin.x) / electrodesSize.x) * brainSize.x;
        pos.y = brainMin.y + ((pos.y - electrodesMin.y) / electrodesSize.y) * brainSize.y;
        pos.z = brainMin.z + ((pos.z - electrodesMin.z) / electrodesSize.z) * brainSize.z;
    }
}

void loadActivityValues(const std::string& filename) {
    std::cout << "Loading activity values from " << filename << std::endl;
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Failed to open activity file.\n";
        exit(1);
    }

    json data;
    file >> data;

    float maxActivity = 0.0f;
    for (auto& [label, value] : data.items()) {
        float val = value;
        activityValues[label] = val;
        maxActivity = std::max(maxActivity, val);
    }

    // Normalize values to 0–1 range
    if (maxActivity > 0.0f) {
        for (auto& [label, value] : activityValues) {
            value /= maxActivity;
        }
    }

    std::cout << "Loaded " << activityValues.size() << " activity values. Max was " << maxActivity << "\n";
}


void activityToColor(float value, float& r, float& g, float& b) {
    value = std::clamp(value, 0.0f, 1.0f);

    // Non-linear boost for visibility
    value = std::sqrt(value); // or try pow(value, 0.4f)

    if (value < 0.5f) {
        r = 0;
        g = value * 2.0f;
        b = 1.0f - g;
    }
    else {
        r = (value - 0.5f) * 2.0f;
        g = 1.0f - r;
        b = 0;
    }
}

void renderBitmapString(float x, float y, void* font, const std::string& text) {
    glRasterPos2f(x, y);
    for (char c : text) {
        glutBitmapCharacter(font, c);
    }
}

void drawBrainModel() {
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, brainVertices.data());
    glColor3f(0.86f, 0.72f, 0.72f);  // Soft pinkish tone

    glDrawElements(GL_TRIANGLES, brainIndices.size(), GL_UNSIGNED_INT, brainIndices.data());
    glDisableClientState(GL_VERTEX_ARRAY);
}

void drawElectrodes() {
    for (const auto& [label, pos] : electrodePositions) {
        auto it = activityValues.find(label);
        if (it == activityValues.end()) continue;

        float activity = it->second;

        float r, g, b;
        activityToColor(activity, r, g, b);
        glColor3f(r, g, b);

        // Draw electrode as a colored sphere
        glPushMatrix();
        glTranslatef(pos.x, pos.y, pos.z);
        glutSolidSphere(0.01, 16, 16);
        glPopMatrix();

        // Draw label and actual activity value
        glColor3f(0.0f, 0.0f, 0.0f);  // Text color: black

        // Format the label and activity value together (e.g. "F3: 0.834")
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%s: %.4f", label.c_str(), activity);
        std::string text = buffer;


        glRasterPos3f(pos.x + 0.025f, pos.y + 0.012f, pos.z);
        for (char c : text) {
            glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);
        }
    }
}
void mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON) {
        leftButtonDown = (state == GLUT_DOWN);
    }
    else if (button == GLUT_RIGHT_BUTTON) {
        rightButtonDown = (state == GLUT_DOWN);
    }

    lastMouseX = x;
    lastMouseY = y;
}
void motion(int x, int y) {
    int dx = x - lastMouseX;
    int dy = y - lastMouseY;

    if (leftButtonDown) {
        rotateX += dy * 0.3f;
        rotationY += dx * 0.3f;
    }
    if (rightButtonDown) {
        zoom += dy * 0.01f;
    }

    lastMouseX = x;
    lastMouseY = y;

    glutPostRedisplay();
}


void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    glTranslatef(0.0f, 0.0f, zoom);
    glRotatef(rotateX, 1.0f, 0.0f, 0.0f);
    glRotatef(rotationY, 0.0f, 1.0f, 0.0f);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glTranslatef(-brainCentre.x, -brainCentre.y, -brainCentre.z);

    glPushMatrix();
    float centerX = (brainMin.x + brainMax.x) / 2.0f;
    float centerY = (brainMin.y + brainMax.y) / 2.0f;
    float centerZ = (brainMin.z + brainMax.z) / 2.0f;
    glTranslatef(centerX, centerY, centerZ);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glTranslatef(-centerX, -centerY, -centerZ);
    drawBrainModel();
    glPopMatrix();

    drawElectrodes();

    glutSwapBuffers();
}

void timer(int) {
    rotationY += 0.1f;
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

void keyboard(unsigned char key, int x, int y) {
    if (key == 27) exit(0);
    glutPostRedisplay();
}

void initOpenGL() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, 1.0f, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    GLfloat light_pos[] = { 0, 0, 2, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);

    GLfloat ambientLight[] = { 0.3f, 0.3f, 0.3f, 1.0f };  // ambient component
    GLfloat diffuseLight[] = { 0.7f, 0.7f, 0.7f, 1.0f };  // diffuse component
    GLfloat specularLight[] = { 0.2f, 0.2f, 0.2f, 1.0f }; // optional

    glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specularLight);

    glEnable(GL_COLOR_MATERIAL);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

int main(int argc, char** argv) {
    loadBrainModel("models/Brain_Model.obj");
    loadElectrodePositions("electrode_positions.json");
    normalizeElectrodePositions();
    loadActivityValues("activity.json");

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(1000, 800);
    glutCreateWindow("3D Brain Activity Visualizer");

    initOpenGL();
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);

    glutTimerFunc(0, timer, 0);

    glutMainLoop();
    return 0;
}
