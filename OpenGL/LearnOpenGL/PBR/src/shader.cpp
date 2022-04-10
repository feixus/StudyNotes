#include "shader.h"

Shader::Shader(const char* a_vertexShaderSource, const char* a_fragmentShaderSource)
{
    setVertexShader(a_vertexShaderSource);
    setFragmentShader(a_fragmentShaderSource);

    compile();
}

Shader::Shader(const char* a_vertexShaderSource, const char* a_fragmentShaderSource, const char* a_tcsShaderSource, const char* a_tesShaderSource, const char* a_gsShaderSource)
{
    setVertexShader(a_vertexShaderSource);
    setFragmentShader(a_fragmentShaderSource);

    if (a_tcsShaderSource != nullptr)
        setTessellationControlShader(a_tcsShaderSource);
    if (a_tesShaderSource != nullptr)
        setTessellationEvalutionShader(a_tesShaderSource);

    if (a_gsShaderSource != nullptr)
        setGeometryShader(a_gsShaderSource);

    compile();
}

// Sets the vertex shader of this program.
bool Shader::setVertexShader(std::string a_pathToShaderSourceFile)
{
    std::string shaderSource = this->loadShaderSource(a_pathToShaderSourceFile);
    this->vertexShaderSource = shaderSource.c_str();

    // Create the new vertex shader.
    this->vertexShaderID = glCreateShader(GL_VERTEX_SHADER);

    // Add the shader code to the shader.
    glShaderSource(this->vertexShaderID, 1, &this->vertexShaderSource, NULL);
    glCompileShader(this->vertexShaderID);

    std::cout << "vertex ID = " << this->vertexShaderID << std::endl;
    if (!this->shaderCompiled(this->vertexShaderID))
        return false;

    return true;
}

bool Shader::setFragmentShader(std::string a_pathToShaderSourceFile)
{
    std::string shaderSource = this->loadShaderSource(a_pathToShaderSourceFile);
    this->fragmentShaderSource = shaderSource.c_str();

    // Create the new vertex shader.
    this->fragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

    // Add the shader code to the shader.
    glShaderSource(this->fragmentShaderID, 1, &this->fragmentShaderSource, NULL);
    glCompileShader(this->fragmentShaderID);

    std::cout << "fragment ID = " << this->fragmentShaderID << std::endl;
    if (!this->shaderCompiled(this->fragmentShaderID))
        return false;

    return true;
}

bool Shader::setTessellationControlShader(std::string a_pathToShaderSourceFile)
{
    std::string shaderSource = this->loadShaderSource(a_pathToShaderSourceFile);
    this->tcsShaderSource = shaderSource.c_str();

    // Create the new vertex shader.
    this->tcsShaderID = glCreateShader(GL_TESS_CONTROL_SHADER);

    // Add the shader code to the shader.
    glShaderSource(this->tcsShaderID, 1, &this->tcsShaderSource, NULL);
    glCompileShader(this->tcsShaderID);

    if (!this->shaderCompiled(this->tcsShaderID))
        return false;

    return true;
}

bool Shader::setTessellationEvalutionShader(std::string a_pathToShaderSourceFile)
{
    std::string shaderSource = this->loadShaderSource(a_pathToShaderSourceFile);
    this->tesShaderSource = shaderSource.c_str();

    // Create the new vertex shader.
    this->tesShaderID = glCreateShader(GL_TESS_EVALUATION_SHADER);

    // Add the shader code to the shader.
    glShaderSource(this->tesShaderID, 1, &this->tesShaderSource, NULL);
    glCompileShader(this->tesShaderID);

    if (!this->shaderCompiled(this->tesShaderID))
        return false;

    return true;
}

bool Shader::setGeometryShader(std::string a_pathToShaderSourceFile)
{
    std::string shaderSource = this->loadShaderSource(a_pathToShaderSourceFile);
    this->gsShaderSource = shaderSource.c_str();

    // Create the new vertex shader.
    this->gsShaderID = glCreateShader(GL_GEOMETRY_SHADER);

    // Add the shader code to the shader.
    glShaderSource(this->gsShaderID, 1, &this->gsShaderSource, NULL);
    glCompileShader(this->gsShaderID);

    if (!this->shaderCompiled(this->gsShaderID))
        return false;

    return true;
}

int Shader::compile()
{
    this->shaderProgram = glCreateProgram();
    
    // Attach the vertex and fragment shader.
    glAttachShader(this->shaderProgram, this->vertexShaderID);
    glAttachShader(this->shaderProgram, this->fragmentShaderID);

    if (this->tesShaderID > 0)
        glAttachShader(this->shaderProgram, this->tesShaderID);
    if (this->tcsShaderID > 0)
        glAttachShader(this->shaderProgram, this->tcsShaderID);

    if (this->gsShaderID > 0)
        glAttachShader(this->shaderProgram, this->gsShaderID);

    // Compile the shader.
    glLinkProgram(this->shaderProgram);

    int compiled;
    char infoLog[512];

    // Get the compile status.
    glGetProgramiv(this->shaderProgram, GL_LINK_STATUS, &compiled);
    if (!compiled)
    {
        glGetShaderInfoLog(this->shaderProgram, 512, NULL, infoLog);
        std::cerr << "ERROR: Could not compile shader program: " << infoLog << std::endl;

        return -1;
    }

    // Delete the shaders.
    glDeleteShader(this->vertexShaderID);
    glDeleteShader(this->fragmentShaderID);

    if (this->tcsShaderID > 0)
        glDeleteShader(this->tcsShaderID);
    if (this->tesShaderID > 0)
        glDeleteShader(this->tesShaderID);

    if (this->gsShaderID > 0)
        glDeleteShader(this->gsShaderID);

    return this->shaderProgram;
}

int Shader::getUniformLocation(std::string a_uniformName)
{
    int m_location = glGetUniformLocation(this->shaderProgram, a_uniformName.c_str());

    return m_location;
}

void Shader::use()
{
    glUseProgram(this->shaderProgram);
}

std::string Shader::loadShaderSource(std::string a_pathToShaderSourceFile)
{
    std::ifstream fileReader;
    fileReader.open(a_pathToShaderSourceFile, std::ios::binary);

    // Make sure to check that the file is open.
    if (fileReader.is_open())
    {
        // Set the 'cursor' at the beginning of the file.
        fileReader.seekg(0, std::ios::beg);

        std::string line;
        std::string output;
        while (std::getline(fileReader, line))
        {
            output.append(line);
            output.append("\n");
        }

        // Add a null terminator at the end of the string.
        output.append("\0");

        return output;
    }
    else
    {
        std::cerr << "Could not open file." << std::endl;
    }

    return "";
}

bool Shader::shaderCompiled(unsigned int a_id)
{
    int compiled;
    char infoLog[512];

    // Get the compile status.
    glGetShaderiv(a_id, GL_COMPILE_STATUS, &compiled);

    if (!compiled)
    {
        glGetShaderInfoLog(a_id, 512, NULL, infoLog);
        std::cerr << a_id << " -->>ERROR: Could not compile shader: " << infoLog << std::endl;

        return false;
    }

    return true;
}

void Shader::setMat4(const std::string &name, const glm::mat4 &mat) const
{
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}

void Shader::setVec3(const std::string &name, const glm::vec3 &vector) const
{
    glUniform3fv(glGetUniformLocation(shaderProgram, name.c_str()), 1, &vector[0]);
}

void Shader::setFloat(const std::string &name, float variable) const
{
    glUniform1f(glGetUniformLocation(shaderProgram, name.c_str()), variable);
}

void Shader::setInt(const std::string &name, int variable) const
{
    glUniform1f(glGetUniformLocation(shaderProgram, name.c_str()), variable);
}