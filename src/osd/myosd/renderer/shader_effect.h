#include <gl/gles2.h>

//TODO: there should be a better way to do this

/*
 * Encapsulates a single screen-filter GLES 2 shader
 * it works on 
 */
class shader_filter
{
public:
	shader_filter(const char* vertex_src, const char* frag_src);

	void update_uniforms(int tex_width, int tex_height);

private:
	//shader program
	GLuint m_program;
};
