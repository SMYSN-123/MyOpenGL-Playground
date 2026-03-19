#ifndef POSTPROCESSOR_H
#define POSTPROCESSOR_H

#include <iostream>
#include <glad/glad.h>

class PostProcessor
{
public:
    static void RenderFullScreen()
    {
        if (emptyVAO == 0)
        {
            glGenVertexArrays(1, &emptyVAO);
        }
        glBindVertexArray(emptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3); 
        glBindVertexArray(0);
    }
private:
    static unsigned int emptyVAO;
};

#endif