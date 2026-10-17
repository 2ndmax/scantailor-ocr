// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FOUNDATION_THREADBUDGET_H_
#define SCANTAILOR_FOUNDATION_THREADBUDGET_H_

/**
 * The number of processor threads the program may use, set once in the settings and valid for
 * every part that works in parallel (batch processing, PDF export, image filters, JPEG 2000).
 *
 * Tasks that run side by side (the pages of the batch processing, the pages of the PDF export)
 * share the threads: inside a TaskScope, a task may use maxThreads() / (number of running tasks)
 * threads of its own, at least one.  So a single page in the preview uses all of them, while a
 * full batch runs every page on one thread, and in total about maxThreads() threads are busy.
 */
class ThreadBudget {
 public:
  /** The threads of this machine (on 32-bit builds at most 2, for the address space). */
  static int availableThreads();

  /** The value when nothing is set: all threads but two, at least one. */
  static int defaultThreads();

  /** The limit, 1 to availableThreads(). */
  static int maxThreads();

  static void setMaxThreads(int threads);

  /** How many threads the work on the current thread may use for itself, at least one. */
  static int threadsForCurrentTask();

  /** Marks the current thread as running one of several tasks that share the threads. */
  class TaskScope {
   public:
    TaskScope();

    ~TaskScope();

    TaskScope(const TaskScope&) = delete;

    TaskScope& operator=(const TaskScope&) = delete;

   private:
    bool m_outer;
  };
};


#endif  // ifndef SCANTAILOR_FOUNDATION_THREADBUDGET_H_
