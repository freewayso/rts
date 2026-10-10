plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.permafrost.engine"
    compileSdk = 34
    // Match installed NDK (folder may be linked as 26.3.11579264 / r26d)
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "com.permafrost.engine"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0-android"

        externalNativeBuild {
            cmake {
                cppFlags += ""
                cFlags += listOf("-D__ANDROID__", "-DPF_GLES", "-DPF_ANDROID_FULL", "-DPF_NO_PYTHON", "-DPF_GLES_WATER_FX=0")
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_ARM_NEON=TRUE",
                    "-DPF_ANDROID_LINK_ENGINE=ON",
                    "-DPF_ANDROID_ENGINE_BATCH=2"
                )
            }
        }

        ndk {
            // Primary ABI per plan; armeabi-v7a optional (Phase 5)
            abiFilters += listOf("arm64-v8a")
            // Uncomment for multi-ABI:
            // abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets {
        getByName("main") {
            // Pack engine assets; shaders_es copied under assets/shaders_es below
            assets.srcDirs(
                "../../assets",
                "src/main/assets"
            )
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

// Keep shaders_es/ at APK assets/shaders_es/ for g_basepath layout
tasks.register<Copy>("syncShadersEs") {
    from("../../shaders_es")
    into("src/main/assets/shaders_es")
}
tasks.named("preBuild") {
    dependsOn("syncShadersEs")
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")
}
