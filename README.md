# Lesson Planner

Weekly lesson planner for Math 87, Algebra 1, Algebra 2, Consumer Math, Science 8,
Biology and Physics. Each class has **Lesson**, **Special** (quizzes, tests, labs,
anything out of the ordinary; highlighted when filled) and **Homework** for every school day.

This is the C++ (Qt 6) version. It uses the same database, lock file, backups and settings
file as the earlier Python version, so both can open the same folder.

- The data is one SQLite file (`lessonplanner.db`) in a folder you choose, meant to be in Nextcloud.
- The lock file `lessonplanner.lock` stops two computers from editing at once. The second
  computer opens **read-only**, and switches to editable when the first one closes.
- A daily backup `lessons_MMDDYYYY.db` is saved in the same folder. The last 30 days are
  kept; you can change that in Options.
- Read-only PDFs of every week are kept up to date in `<folder>/Tablet/` so you can view
  them on the Android tablet with the Nextcloud app.
- The app follows the system light/dark mode.

## Build and run (Linux)

Needs CMake, a C++20 compiler, Qt 6.5 or newer and SQLite. On Arch:

```sh
sudo pacman -S --needed cmake qt6-base sqlite
cmake -S . -B build
cmake --build build -j
./build/lessonplanner
```

On first run the app asks for the database folder, e.g. `~/Nextcloud/Documents/School`.
If that folder has no `lessonplanner.db`, it asks before creating one. This choice is
saved per computer (File → Options). Set `LESSONPLANNER_DB_FOLDER` to override it for
one run.

## Using it

- Tabs are weeks, newest on the left. The current week opens at startup. Use **+** (Ctrl+N)
  to add the next week, and Ctrl+T to jump back to this week.
- Edits save automatically.
- Right-click a class/day:
  - **Insert lesson here**: that class's lessons from this day on move one school day later.
  - **Remove this lesson**: deletes it and pulls that class's later lessons one school day earlier.
  - **Mark day as No School**: every class shifts one day later. Right-click the
    No School row to undo it.
- File → Print week / Export week to PDF prints one landscape page per week.

## Windows download

GitHub Actions builds the Windows program, so the school computer needs nothing installed.
Push a version tag to publish a release:

```sh
git tag v0.3.0
git push origin v0.3.0
```

When the **Build Windows** workflow finishes, download `LessonPlanner-windows.zip` from the
repo's Releases page and unzip it anywhere. Run `LessonPlanner\LessonPlanner.exe`.
You can also start the workflow by hand from the Actions tab and download the zip from that run.

To build locally on Windows instead, install Qt 6 (MSVC) and CMake, then from a
"x64 Native Tools" prompt with Qt's `bin` folder on the PATH:

```bat
cmake -S . -B build
cmake --build build --config Release
cmake --install build --config Release --prefix dist\LessonPlanner
windeployqt --release --no-translations --no-compiler-runtime dist\LessonPlanner\LessonPlanner.exe
```

## Development

```sh
ctest --test-dir build --output-on-failure
```

The `gui` test drives the real window without needing a screen. The code is laid out as
`src/core` (calendar, database, shifting, lock, backup; no window needed) and `src/gui`.
