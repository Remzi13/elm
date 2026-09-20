* перейти не RenderFrame, избавиться от передачи настроек в рендер 
```cpp
  struct RenderObject
  {
      MeshId mesh;
      Matrix4x4 transform;
      Vector4 color;
  };

  struct RenderFrame
  {
      Camera camera;
      Vector<RenderObject> objects;
  };
```
 * отделить RenderPipliene
 * доделать ситему логирования 