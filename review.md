Resolution: 1280x720
FPS: 40


| mode     | context  | cpu(%) | gpu(%) | effect                                                   |
| --       | --       | --     | --     | ---                                                      |
| passive  | egl      | 75     | 60     | good, picture blur when steady                           |
| active   | egl      | 75     | 50     | bad, wrong data in picture when start move and it's slow |
| passive  | glut     | 94     | 80     | good, picture blur when steady                           |
| active   | glut     | 99     | 87     | bad, wrong data in picture when start move and it's slow |
| filament | filament | 75     | 60     |


