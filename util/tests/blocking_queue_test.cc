/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gtest/gtest.h"

#include "chre/util/fixed_size_blocking_queue.h"

using chre::FixedSizeBlockingQueue;

TEST(FixedSizeBlockingQueue, IsEmptyByDefault) {
  FixedSizeBlockingQueue<int, 16> blockingQueue;
  ASSERT_TRUE(blockingQueue.empty());
}

TEST(FixedSizeBlockingQueue, PushPopVerifyOrder) {
  FixedSizeBlockingQueue<int, 16> blockingQueue;

  blockingQueue.push(0x1337);
  blockingQueue.push(0xcafe);

  ASSERT_EQ(blockingQueue.pop(), 0x1337);
  ASSERT_EQ(blockingQueue.pop(), 0xcafe);
}
