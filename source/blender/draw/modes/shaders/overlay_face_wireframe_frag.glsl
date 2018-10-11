#ifndef SELECT_EDGES
uniform vec3 wireColor;
uniform vec3 rimColor;

in float facing;
in vec3 barycentric;

#  ifdef LIGHT_EDGES
flat in vec3 edgeSharpness;
#  endif

out vec4 fragColor;
#endif

float min_v3(vec3 v) { return min(v.x, min(v.y, v.z)); }
float max_v3(vec3 v) { return max(v.x, max(v.y, v.z)); }

/* In pixels */
const float wire_size = 0.0; /* Expands the core of the wire (part that is 100% wire color) */
const float wire_smooth = 1.2; /* Smoothing distance after the 100% core. */

/* Alpha constants could be exposed in the future. */
const float front_alpha = 0.35;
const float rim_alpha = 0.75;

void main()
{
#ifndef SELECT_EDGES
	vec3 dx = dFdx(barycentric);
	vec3 dy = dFdy(barycentric);
	vec3 d = vec3(
		length(vec2(dx.x, dy.x)),
		length(vec2(dx.y, dy.y)),
		length(vec2(dx.z, dy.z))
	);
	vec3 dist_to_edge = barycentric / d;

#  ifdef LIGHT_EDGES
	vec3 fac = abs(dist_to_edge);
#  else
	float fac = min_v3(abs(dist_to_edge));
#  endif

	fac = smoothstep(wire_size + wire_smooth, wire_size, fac);

	float facing_clamped = clamp((gl_FrontFacing) ? facing : -facing, 0.0, 1.0);

	vec3 final_front_col = mix(rimColor, wireColor, 0.05);
	fragColor = mix(vec4(rimColor, rim_alpha), vec4(final_front_col, front_alpha), facing_clamped);

#  ifdef LIGHT_EDGES
	fragColor.a *= max_v3(fac * edgeSharpness);
#  else
	fragColor.a *= fac;
#  endif
#endif
}
