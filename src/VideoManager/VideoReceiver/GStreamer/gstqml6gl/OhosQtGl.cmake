function(qgc_prepare_ohos_qt6_gl source destination)
    file(COPY "${source}/" DESTINATION "${destination}")
    set(utility "${destination}/gstqt6glutility.cc")
    file(READ "${utility}" contents)
    set(include_guard "defined (HAVE_QT_EGLFS) || defined (HAVE_QT_ANDROID)")
    string(FIND "${contents}" "${include_guard}" include_position)
    if(include_position EQUAL -1)
        message(FATAL_ERROR "Unsupported GStreamer Qt GL utility: missing EGL include guard")
    endif()
    string(REPLACE "${include_guard}" "${include_guard} || defined (HAVE_QT_OHOS)" contents "${contents}")
    set(display_anchor "  if (!display)\n    display = gst_gl_display_new ();")
    string(FIND "${contents}" "${display_anchor}" display_position)
    if(display_position EQUAL -1)
        message(FATAL_ERROR "Unsupported GStreamer Qt GL utility: missing display selection")
    endif()
    set(ohos_display [=[
#if GST_GL_HAVE_PLATFORM_EGL && defined (HAVE_QT_OHOS)
  if (QStringLiteral ("ohos") == app->platformName()) {
    /* QOhosEgl uses eglGetDisplay(EGL_DEFAULT_DISPLAY). Its native interface
     * does not expose egldisplay. The render thread may already have it current. */
    EGLDisplay egl_display = eglGetCurrentDisplay ();
    if (egl_display == EGL_NO_DISPLAY)
      egl_display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
    if (egl_display != EGL_NO_DISPLAY) {
      GstGLDisplayEGL *display_egl = gst_gl_display_egl_new_with_egl_display (egl_display);
      if (display_egl) {
        gst_gl_display_egl_set_foreign (display_egl, TRUE);
        display = GST_GL_DISPLAY (display_egl);
      }
    }
    if (!display) {
      GST_ERROR ("Cannot obtain the Qt OHOS EGL display");
      G_UNLOCK (display_lock);
      return NULL;
    }
  }
#endif
]=])
    string(REPLACE "${display_anchor}" "${ohos_display}\n${display_anchor}" contents "${contents}")
    string(REPLACE "#if GST_GL_HAVE_PLATFORM_EGL && defined (HAVE_QT_EGLFS)"
        "#if GST_GL_HAVE_PLATFORM_EGL && (defined (HAVE_QT_EGLFS) || defined (HAVE_QT_OHOS))"
        contents "${contents}")
    set(context_anchor "  gl_api = gst_gl_context_get_current_gl_api (platform, NULL, NULL);")
    string(FIND "${contents}" "${context_anchor}" context_position)
    if(context_position EQUAL -1)
        message(FATAL_ERROR "Unsupported GStreamer Qt GL utility: missing current context lookup")
    endif()
    set(ohos_context [=[
#if GST_GL_HAVE_PLATFORM_EGL && defined (HAVE_QT_OHOS)
  if (QStringLiteral ("ohos") == QGuiApplication::platformName()) {
    if (platform != GST_GL_PLATFORM_EGL || !QOpenGLContext::currentContext () ||
        eglGetCurrentContext () == EGL_NO_CONTEXT ||
        eglGetCurrentDisplay () != (EGLDisplay) gst_gl_display_get_handle (display)) {
      GST_ERROR ("Qt OHOS has no current OpenGL context on the shared EGL display");
      return FALSE;
    }
  }
#endif
]=])
    string(REPLACE "${context_anchor}" "${ohos_context}\n${context_anchor}" contents "${contents}")
    string(REPLACE "#include <QtGui/QGuiApplication>" "#include <QtGui/QGuiApplication>\n#include <QtGui/QOpenGLContext>" contents "${contents}")
    file(WRITE "${utility}" "${contents}")
endfunction()
