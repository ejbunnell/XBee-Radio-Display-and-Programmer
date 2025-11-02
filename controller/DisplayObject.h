class DisplayObject {
  public:
    DisplayObject(const size_t bufferSize, const char* defaultBuffer = "") : bufferSize(bufferSize) {
      buffer = new char[bufferSize];
      
    }

    ~DisplayObject() {
      delete[] buffer;
    }

  private:
    char *buffer;
    const size_t bufferSize;
};